#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/DailyLevelPage.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace {
	constexpr char const* ALLOWED_IDS_URL = "https://audio.cheesecdn.com/ids.txt.txt";
	// Offset keeps our daily ID clear of any real Event IDs stored by the game.
	constexpr int DD_ID_OFFSET = 700000;

	bool g_ddMode = false;
	int g_ddLevelID = 0;
	int g_ddDailyID = 0;

	EventListener<web::WebTask> g_fetchListener;
	EventListener<web::WebTask> g_idsListener;
	EventListener<web::WebTask> g_setListener;

	// 0 = unknown/loading, 1 = allowed, -1 = not allowed
	int g_allowed = 0;

	std::string serverURL(std::string const& path) {
		auto base = Mod::get()->getSettingValue<std::string>("server");
		while (!base.empty() && base.back() == '/') base.pop_back();
		return base + "/" + path;
	}

	int myUserID() {
		return GameManager::get()->m_playerUserID.value();
	}

	void checkAllowed(std::function<void(bool)> cb) {
		if (g_allowed != 0) return cb(g_allowed > 0);
		g_idsListener.bind([cb](web::WebTask::Event* e) {
			auto* res = e->getValue();
			if (!res) return;
			bool ok = false;
			if (res->ok()) {
				auto body = res->string().unwrapOr("");
				std::string tok;
				auto flush = [&]() {
					if (!tok.empty()) {
						if (auto id = numFromString<int>(tok); id.isOk() && id.unwrap() == myUserID()) ok = true;
						tok.clear();
					}
				};
				for (char c : body) {
					if (c >= '0' && c <= '9') tok += c; else flush();
				}
				flush();
				g_allowed = ok ? 1 : -1;
			}
			cb(ok);
		});
		g_idsListener.setFilter(web::WebRequest().timeout(std::chrono::seconds(8)).get(ALLOWED_IDS_URL));
	}

	void failStatus(GJErrorCode code) {
		auto mgr = GameLevelManager::sharedState();
		if (mgr->m_GJDailyLevelDelegate) mgr->m_GJDailyLevelDelegate->dailyStatusFailed(GJTimedLevelType::Event, code);
	}

	void fetchDailyDemon() {
		g_fetchListener.bind([](web::WebTask::Event* e) {
			auto* res = e->getValue();
			if (!res) return;
			if (!g_ddMode) return;
			if (!res->ok()) return failStatus(GJErrorCode::GenericError);

			auto body = res->string().unwrapOr("");
			auto parts = utils::string::split(body, "|");
			if (parts.size() != 3) return failStatus(GJErrorCode::NotFound); // "-1" = nothing set for today

			auto day = numFromString<int>(parts[0]);
			auto left = numFromString<int>(parts[1]);
			auto lvl = numFromString<int>(parts[2]);
			if (day.isErr() || left.isErr() || lvl.isErr() || lvl.unwrap() <= 0) return failStatus(GJErrorCode::NotFound);

			g_ddLevelID = lvl.unwrap();
			g_ddDailyID = DD_ID_OFFSET + (day.unwrap() % 100000);

			auto mgr = GameLevelManager::sharedState();
			mgr->storeDailyLevelState(g_ddDailyID, left.unwrap(), GJTimedLevelType::Event);
			if (mgr->m_GJDailyLevelDelegate) mgr->m_GJDailyLevelDelegate->dailyStatusFinished(GJTimedLevelType::Event);
		});
		g_fetchListener.setFilter(web::WebRequest().timeout(std::chrono::seconds(10)).get(serverURL("getGJDailyDemon.php")));
	}
}

// ---- set-level popup (only reachable for users in ids.txt) ----------------------------------

class DDSetPopup : public geode::Popup<> {
protected:
	TextInput* m_input = nullptr;

	bool setup() override {
		this->setTitle("Set Daily Demon");
		m_input = TextInput::create(160.f, "Level ID", "bigFont.fnt");
		m_input->setFilter(CommonFilter::Uint);
		m_input->setMaxCharCount(10);
		m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0.f, 8.f});

		auto info = CCLabelBMFont::create("Sets today's (UTC) Daily Demon", "chatFont.fnt");
		info->setScale(.6f);
		info->setOpacity(160);
		m_mainLayer->addChildAtPosition(info, Anchor::Center, {0.f, -18.f});

		auto btn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Set"), this, menu_selector(DDSetPopup::onSet));
		m_buttonMenu->addChildAtPosition(btn, Anchor::Bottom, {0.f, 24.f});
		return true;
	}

	void onSet(CCObject*) {
		auto id = numFromString<int>(m_input->getString());
		if (id.isErr() || id.unwrap() <= 0) {
			return Notification::create("Enter a valid level ID", NotificationIcon::Error)->show();
		}
		auto req = web::WebRequest()
			.timeout(std::chrono::seconds(10))
			.header("Content-Type", "application/x-www-form-urlencoded")
			.bodyString(fmt::format("userID={}&levelID={}", myUserID(), id.unwrap()));

		Ref<DDSetPopup> self = this;
		g_setListener.bind([self](web::WebTask::Event* e) {
			auto* res = e->getValue();
			if (!res) return;
			auto body = res->string().unwrapOr("");
			if (res->ok() && body == "1") {
				Notification::create("Daily Demon set!", NotificationIcon::Success)->show();
				self->onClose(nullptr);
			} else if (body == "-2") {
				Notification::create("Not authorised", NotificationIcon::Error)->show();
			} else {
				Notification::create(fmt::format("Failed ({})", body.empty() ? "no response" : body), NotificationIcon::Error)->show();
			}
		});
		g_setListener.setFilter(req.post(serverURL("setGJDDLevel.php")));
	}

public:
	static DDSetPopup* create() {
		auto ret = new DDSetPopup();
		if (ret->initAnchored(260.f, 140.f)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

// ---- the popup itself ---------------------------------------------------------------------

class $modify(DDPage, DailyLevelPage) {
	struct Fields {
		bool m_isDD = false;
	};

	bool init(GJTimedLevelType type) {
		if (!DailyLevelPage::init(type)) return false;
		if (!g_ddMode || type != GJTimedLevelType::Event) return true;
		m_fields->m_isDD = true;

		// Swap the "EVENT" title sprite for a Daily Demon label.
		auto frame = CCSpriteFrameCache::get()->spriteFrameByName("eventLevelLabel_001.png");
		if (frame && m_mainLayer) {
			for (auto child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
				auto spr = typeinfo_cast<CCSprite*>(child);
				if (spr && spr->isFrameDisplayed(frame)) {
					auto label = CCLabelBMFont::create("Daily Demon", "goldFont.fnt");
					label->setPosition(spr->getPosition());
					label->setScale(.8f);
					spr->getParent()->addChild(label, spr->getZOrder());
					spr->setVisible(false);
					break;
				}
			}
		}

		// "Set" button for whitelisted users.
		Ref<DailyLevelPage> self = this;
		checkAllowed([self](bool ok) {
			if (!ok || !self->getParent() || !self->m_buttonMenu) return;
			auto spr = ButtonSprite::create("Set", "goldFont.fnt", "GJ_button_04.png", .6f);
			auto btn = CCMenuItemSpriteExtra::create(spr, self, menu_selector(DDPage::onSetDD));
			btn->setID("set-button");
			auto win = CCDirector::get()->getWinSize();
			btn->setPosition(self->m_buttonMenu->convertToNodeSpace({win.width / 2.f + 150.f, win.height / 2.f - 100.f}));
			self->m_buttonMenu->addChild(btn);
		});
		return true;
	}

	void onSetDD(CCObject*) {
		if (auto p = DDSetPopup::create()) p->show();
	}

	void onClose(CCObject* sender) {
		if (m_fields->m_isDD) g_ddMode = false;
		DailyLevelPage::onClose(sender);
	}

	void keyBackClicked() {
		if (m_fields->m_isDD) g_ddMode = false;
		DailyLevelPage::keyBackClicked();
	}

	void levelDownloadFinished(GJGameLevel* level) {
		if (m_fields->m_isDD && level && g_ddDailyID > 0) {
			// The page only builds its node for levels carrying a daily ID.
			level->m_dailyID = g_ddDailyID;
			if (auto mgr = GameLevelManager::sharedState(); mgr->m_dailyLevels) {
				mgr->m_dailyLevels->setObject(level, g_ddDailyID);
			}
		}
		DailyLevelPage::levelDownloadFinished(level);
	}
};

class $modify(DDManager, GameLevelManager) {
	bool getGJDailyLevelState(GJTimedLevelType type) {
		if (g_ddMode && type == GJTimedLevelType::Event) {
			fetchDailyDemon();
			return true;
		}
		return GameLevelManager::getGJDailyLevelState(type);
	}

	void downloadLevel(int id, bool gauntlet) {
		// Daily/weekly/event pages request negative IDs (-1/-2/-3); give them our real level instead.
		if (g_ddMode && id < 0 && g_ddLevelID > 0) id = g_ddLevelID;
		GameLevelManager::downloadLevel(id, gauntlet);
	}
};

// ---- Versus -> Daily Demon ----------------------------------------------------------------

class $modify(DDCreatorLayer, CreatorLayer) {
	bool init() {
		if (!CreatorLayer::init()) return false;
		this->replaceVersusButton();
		return true;
	}

	void replaceVersusButton() {
		auto versusFrame = CCSpriteFrameCache::get()->spriteFrameByName("GJ_versusBtn_001.png");
		if (!versusFrame) return;

		for (auto node : CCArrayExt<CCNode*>(this->getChildren())) {
			auto menu = typeinfo_cast<CCMenu*>(node);
			if (!menu) continue;
			for (auto item : CCArrayExt<CCNode*>(menu->getChildren())) {
				auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(item);
				if (!btn) continue;
				auto oldSpr = typeinfo_cast<CCSprite*>(btn->getNormalImage());
				if (!oldSpr || !oldSpr->isFrameDisplayed(versusFrame)) continue;

				// Build the new sprite: our background + the demon face from the game's own sheet.
				auto bg = CCSprite::create(Mod::get()->expandSpriteName("DD_btn_001.png").data());
				if (!bg) return;
				bg->setScale((oldSpr->getContentSize().width * oldSpr->getScale()) / bg->getContentSize().width);
				auto size = bg->getContentSize();
				// Featured Easy Demon icon: easy demon face over the featured glow coin.
				auto center = CCPoint{size.width / 2.f, size.height * .58f};
				if (auto coin = CCSprite::createWithSpriteFrameName("GJ_featuredCoin_001.png")) {
					coin->setScale(1.9f);
					coin->setPosition(center);
					bg->addChild(coin);
				}
				if (auto face = CCSprite::createWithSpriteFrameName("diffIcon_07_btn_001.png")) {
					face->setScale(1.9f);
					face->setPosition(center);
					bg->addChild(face);
				}

				auto newBtn = CCMenuItemSpriteExtra::create(bg, this, menu_selector(DDCreatorLayer::onDailyDemon));
				newBtn->setID("daily-demon-button");
				newBtn->setPosition(btn->getPosition());
				newBtn->setZOrder(btn->getZOrder());
				menu->addChild(newBtn);
				btn->removeFromParent();
				return;
			}
		}
		log::warn("Versus button not found in CreatorLayer; Daily Demon button not added");
	}

	void onDailyDemon(CCObject*) {
		g_ddMode = true;
		g_ddLevelID = 0;
		g_ddDailyID = 0;
		DailyLevelPage::create(GJTimedLevelType::Event)->show();
	}
};
