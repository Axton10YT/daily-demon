// Daily Demon - replaces the Versus button in CreatorLayer with a Daily Demon button.
//
// The popup is the game's own DailyLevelPage opened as the Event type, so it looks and behaves
// like the Event level popup. While that popup is open ("DD mode") three calls are redirected:
//   GameLevelManager::getGJDailyLevelState(Event) -> GET {server}/getGJDailyDemon.php  ("dayID|secondsLeft|levelID")
//   GameLevelManager::downloadLevel(<negative id>, .., dailyID) -> downloads the real levelID from the endpoint
//   DailyLevelPage::levelDownloadFinished          -> tags the level with a daily ID so the node is built
//
// Users listed in ids.txt also get a "Set DD" button on level pages that POSTs to setGJDDLevel.php.

#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/DailyLevelPage.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <ctime>
#include <set>

using namespace geode::prelude;

namespace {
		// Offset keeps our daily ID clear of any real Event IDs stored by the game.
	constexpr int DD_ID_OFFSET = 700000;

	bool g_ddMode = false;
	int g_ddLevelID = 0;
	int g_ddDailyID = 0;
	int g_ddNumber = 0;          // "Daily Demon #N"
	std::time_t g_ddEndsAt = 0;  // when today's demon expires (unix time)

	// Geode v5: web requests are futures; a TaskHolder aborts the task when replaced/destroyed
	// and runs the callback on the main thread.
	async::TaskHolder<web::WebResponse> g_fetchHolder;
	async::TaskHolder<web::WebResponse> g_idsHolder;

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
		g_idsHolder.spawn(
			web::WebRequest().timeout(std::chrono::seconds(8)).get(serverURL("getGJDDIDs.php")),
			[cb](web::WebResponse res) {
				bool ok = false;
				if (res.ok()) {
					auto body = res.string().unwrapOr("");
					std::string tok;
					auto flush = [&]() {
						if (!tok.empty()) {
							if (auto id = utils::numFromString<int>(tok); id.isOk() && id.unwrap() == myUserID()) ok = true;
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
			}
		);
	}

	// The popup borrows the game's Event slot. Stash the real Event state while it is open
	// (so cached Event data never shows up here, and ours never leaks into the real Event button).
	struct SavedEvent { int id = 0, timeLeft = 0, active = 0; bool has = false; } g_saved;

	void beginDDMode() {
		auto mgr = GameLevelManager::sharedState();
		if (!g_saved.has) {
			g_saved = { mgr->m_eventID, mgr->m_eventTimeLeft, mgr->m_activeEventID, true };
		}
		mgr->m_eventID = 0;
		mgr->m_eventTimeLeft = 0;
		mgr->m_activeEventID = 0;
		g_ddLevelID = 0;
		g_ddDailyID = 0;
		g_ddNumber = 0;
		g_ddEndsAt = 0;
		g_ddMode = true;
	}

	// Entries we put into the manager's daily-level dictionary. They must never survive into the
	// game's save data, so they are removed when the popup closes and right before every save.
	std::set<int> g_injected;

	void purgeInjected() {
		if (g_injected.empty()) return;
		auto mgr = GameLevelManager::sharedState();
		if (mgr && mgr->m_dailyLevels) {
			for (int id : g_injected) {
				if (auto lvl = static_cast<GJGameLevel*>(mgr->m_dailyLevels->objectForKey(id))) {
					lvl->m_dailyID = 0;
					mgr->m_dailyLevels->removeObjectForKey(id);
				}
			}
		}
		g_injected.clear();
	}

	void restoreEventState() {
		if (!g_saved.has) return;
		auto mgr = GameLevelManager::sharedState();
		mgr->m_eventID = g_saved.id;
		mgr->m_eventTimeLeft = g_saved.timeLeft;
		mgr->m_activeEventID = g_saved.active;
		g_saved.has = false;
	}

	void endDDMode() {
		g_ddMode = false;
		purgeInjected();
		restoreEventState();
	}

	std::string lower(std::string s) {
		for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	bool startsWith(std::string const& s, char const* prefix) {
		return s.rfind(prefix, 0) == 0;
	}

	std::string countdownText() {
		long left = static_cast<long>(g_ddEndsAt - std::time(nullptr));
		if (left < 0) left = 0;
		return fmt::format("New Daily Demon in: {:02}:{:02}:{:02}", left / 3600, (left / 60) % 60, left % 60);
	}

	void collectLabels(CCNode* node, std::vector<CCLabelBMFont*>& out) {
		if (!node) return;
		for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
			if (auto lbl = typeinfo_cast<CCLabelBMFont*>(child)) out.push_back(lbl);
			collectLabels(child, out);
		}
	}

	void hideNear(CCNode* node, CCPoint worldPos) {
		if (!node) return;
		for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
			bool clickable = typeinfo_cast<CCMenuItem*>(child) != nullptr;
			if (clickable && child->isVisible()) {
				auto wp = child->getParent()->convertToWorldSpace(child->getPosition());
				float dx = wp.x - worldPos.x, dy = wp.y - worldPos.y;
				if (dx > 0.f && dx < 110.f && std::abs(dy) < 30.f) {
					child->setVisible(false);
					if (auto item = typeinfo_cast<CCMenuItem*>(child)) item->setEnabled(false);
					continue;
				}
			}
			hideNear(child, worldPos);
		}
	}

	// Event pages carry a reward chest in the bottom centre. Daily Demons have no chest.
	void hideChest(CCNode* node, CCNode* layer) {
		if (!node || !layer) return;
		auto size = layer->getContentSize();
		for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
			if (!child->isVisible()) continue;
			bool candidate = typeinfo_cast<CCSprite*>(child) || typeinfo_cast<CCMenuItem*>(child);
			if (candidate && !typeinfo_cast<CCScale9Sprite*>(child) && !typeinfo_cast<CCLabelBMFont*>(child)) {
				auto p = layer->convertToNodeSpace(child->getParent()->convertToWorldSpace(child->getPosition()));
				if (std::abs(p.x - size.width / 2.f) < size.width * .12f && p.y < size.height * .30f) {
					child->setVisible(false);
					if (auto item = typeinfo_cast<CCMenuItem*>(child)) item->setEnabled(false);
					continue;
				}
			}
			hideChest(child, layer);
		}
	}

	// The popup is the Event page, so strip/relabel everything that says "Event".
	void tidyDDPage(CCNode* page, CCNode* layer = nullptr) {
		std::vector<CCLabelBMFont*> labels;
		collectLabels(page, labels);
		if (layer) {
			hideChest(layer, layer);
			// The lowest visible label under the popup is the timer/flavour text: make it an exact countdown.
			CCLabelBMFont* lowest = nullptr;
			float lowY = 1e9f;
			for (auto lbl : labels) {
				if (!lbl->isVisible()) continue;
				if (typeinfo_cast<CCMenuItem*>(lbl->getParent())) continue;
				float y = lbl->getParent()->convertToWorldSpace(lbl->getPosition()).y;
				if (y < lowY) { lowY = y; lowest = lbl; }
			}
			if (lowest && g_ddEndsAt > 0) lowest->setString(countdownText().c_str());
		}
		for (auto lbl : labels) {
			std::string t = lbl->getString();
			std::string l = lower(t);
			if (startsWith(t, "Event #")) {
				lbl->setString(fmt::format("Daily Demon #{}", g_ddNumber > 0 ? g_ddNumber : 1).c_str());
			} else if (startsWith(t, "Current:")) {
				lbl->setVisible(false);
			} else if (l == "bonus:") {
				lbl->setVisible(false);
				auto wp = lbl->getParent()->convertToWorldSpace(lbl->getPosition());
				hideNear(page, wp); // the reward chest sits just to the right of the label
			} else if (l.find("something epic") != std::string::npos || startsWith(t, "New Daily Demon in:")) {
				lbl->setString(countdownText().c_str());
			}
		}
	}

	void failStatus(GJErrorCode code) {
		if (code == GJErrorCode::NotFound) {
			Notification::create("No Daily Demon has been set for today", NotificationIcon::Info)->show();
		} else {
			Notification::create("Couldn't reach the Daily Demon server", NotificationIcon::Error)->show();
		}
		auto mgr = GameLevelManager::sharedState();
		if (mgr->m_GJDailyLevelDelegate) mgr->m_GJDailyLevelDelegate->dailyStatusFailed(GJTimedLevelType::Event, code);
	}

	void fetchDailyDemon() {
		g_fetchHolder.spawn(
			web::WebRequest().timeout(std::chrono::seconds(10)).get(serverURL("getGJDailyDemon.php")),
			[](web::WebResponse res) {
				if (!g_ddMode) return;
				if (!res.ok()) return failStatus(GJErrorCode::GenericError);

				auto body = res.string().unwrapOr("");
				auto parts = utils::string::split(body, "|");
				if (parts.size() < 3) return failStatus(GJErrorCode::NotFound); // "-1" = nothing set for today

				auto day = utils::numFromString<int>(parts[0]);
				auto left = utils::numFromString<int>(parts[1]);
				auto lvl = utils::numFromString<int>(parts[2]);
				if (day.isErr() || left.isErr() || lvl.isErr() || lvl.unwrap() <= 0) return failStatus(GJErrorCode::NotFound);

				g_ddLevelID = lvl.unwrap();
				g_ddNumber = 0;
				if (parts.size() >= 4) {
					if (auto n = utils::numFromString<int>(parts[3]); n.isOk()) g_ddNumber = n.unwrap();
				}
				g_ddEndsAt = std::time(nullptr) + left.unwrap();
				g_ddDailyID = DD_ID_OFFSET + (day.unwrap() % 100000);

				auto mgr = GameLevelManager::sharedState();
				mgr->storeDailyLevelState(g_ddDailyID, left.unwrap(), GJTimedLevelType::Event);
				if (mgr->m_GJDailyLevelDelegate) mgr->m_GJDailyLevelDelegate->dailyStatusFinished(GJTimedLevelType::Event);
			}
		);
	}
}

// ---- the popup itself ---------------------------------------------------------------------

class $modify(DDPage, DailyLevelPage) {
	struct Fields {
		bool m_isDD = false;
	};

	bool init(GJTimedLevelType type) {
		if (!DailyLevelPage::init(type)) return false;
		if (!g_ddMode || type != GJTimedLevelType::Event) return true;
		m_fields->m_isDD = true;

		// Swap the "EVENT" title sprite for the Daily Demon logo.
		auto frame = CCSpriteFrameCache::get()->spriteFrameByName("eventLevelLabel_001.png");
		if (frame && m_mainLayer) {
			for (auto child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
				auto spr = typeinfo_cast<CCSprite*>(child);
				if (spr && spr->isFrameDisplayed(frame)) {
					auto logo = CCSprite::create(Mod::get()->expandSpriteName("DD_title.png").c_str());
					if (logo && m_buttonMenu) {
						logo->setScale(150.f / logo->getContentSize().width);
						// Tapping the logo requests permission to set Daily Demons.
						auto btn = CCMenuItemSpriteExtra::create(logo, this, menu_selector(DDPage::onLogo));
						btn->setID("dd-logo");
						auto world = spr->getParent()->convertToWorldSpace(spr->getPosition());
						btn->setPosition(m_buttonMenu->convertToNodeSpace(world));
						m_buttonMenu->addChild(btn);
					}
					spr->setVisible(false);
					break;
				}
			}
		}

		// Light red panel inside the popup frame (a child of the background so it draws right above it).
		if (m_mainLayer) {
			CCScale9Sprite* bg = nullptr;
			float best = 0.f;
			for (auto child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
				if (auto s9 = typeinfo_cast<CCScale9Sprite*>(child)) {
					auto sz = s9->getContentSize();
					if (sz.width * sz.height > best) { best = sz.width * sz.height; bg = s9; }
				}
			}
			if (bg) {
				auto sz = bg->getContentSize();
				auto panel = CCLayerColor::create({255, 135, 135, 255}, sz.width - 12.f, sz.height - 12.f);
				panel->setPosition({6.f, 6.f});
				bg->addChild(panel, 1);
			}
		}

		return true;
	}

	void createDailyNode(GJGameLevel* level, bool instant, float delay, bool isNew) {
		DailyLevelPage::createDailyNode(level, instant, delay, isNew);
		if (m_fields->m_isDD) tidyDDPage(this, m_mainLayer);
	}

	void updateTimers(float dt) {
		DailyLevelPage::updateTimers(dt);
		if (m_fields->m_isDD) tidyDDPage(this, m_mainLayer);
	}

	void onClose(CCObject* sender) {
		if (m_fields->m_isDD) endDDMode();
		DailyLevelPage::onClose(sender);
	}

	void onLogo(CCObject*) {
		checkAllowed([](bool ok) {
			if (ok) {
				return Notification::create("You can already set Daily Demons (use Set DD on a level page)", NotificationIcon::Info)->show();
			}
			createQuickPopup(
				"Daily Demon",
				"Want to be able to <cy>set Daily Demons</c>? Send a request and it will be reviewed.",
				"Cancel", "Request",
				[](FLAlertLayer*, bool yes) {
					if (!yes) return;
					auto req = web::WebRequest();
					req.timeout(std::chrono::seconds(10));
					req.header("Content-Type", "application/x-www-form-urlencoded");
					req.bodyString(fmt::format("userID={}&userName={}", myUserID(), utils::string::replace(std::string(GameManager::get()->m_playerName), " ", "_")));
					static async::TaskHolder<web::WebResponse> holder;
					holder.spawn(req.post(serverURL("requestGJDDAccess.php")), [](web::WebResponse res) {
						auto body = res.string().unwrapOr("");
						if (res.ok() && body == "1") Notification::create("Request sent!", NotificationIcon::Success)->show();
						else if (body == "3") Notification::create("Your request is already pending", NotificationIcon::Info)->show();
						else if (body == "2") Notification::create("You already have access", NotificationIcon::Info)->show();
						else if (body == "-2") Notification::create("Your request was declined", NotificationIcon::Error)->show();
						else Notification::create("Couldn't send the request", NotificationIcon::Error)->show();
					});
				}
			);
		});
	}

	void onTheSafe(CCObject* sender) {
		if (!m_fields->m_isDD) return DailyLevelPage::onTheSafe(sender);
		// The Safe lists every level that was ever a Daily Demon.
		Ref<DDPage> self = this;
		static async::TaskHolder<web::WebResponse> holder;
		holder.spawn(
			web::WebRequest().timeout(std::chrono::seconds(10)).get(serverURL("getGJDDList.php")),
			[self](web::WebResponse res) {
				auto body = res.string().unwrapOr("");
				if (!res.ok() || body.empty() || body == "-1") {
					return Notification::create("No Daily Demons yet", NotificationIcon::Info)->show();
				}
				auto search = GJSearchObject::create(SearchType::MapPackOnClick, body);
				CCDirector::get()->pushScene(CCTransitionFade::create(.5f, LevelBrowserLayer::scene(search)));
			}
		);
	}

	void keyBackClicked() {
		if (m_fields->m_isDD) endDDMode();
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
	void encodeDataTo(DS_Dictionary* dict) {
		// Saving on exit: make sure none of our temporary state is written/cleaned up.
		purgeInjected();
		restoreEventState();
		GameLevelManager::encodeDataTo(dict);
	}

	void cleanupDailyLevels() {
		purgeInjected();
		GameLevelManager::cleanupDailyLevels();
	}

	bool getGJDailyLevelState(GJTimedLevelType type) {
		if (g_ddMode && type == GJTimedLevelType::Event) {
			fetchDailyDemon();
			return true;
		}
		return GameLevelManager::getGJDailyLevelState(type);
	}

	void downloadLevel(int id, bool gauntlet, int dailyID) {
		// Daily/weekly/event pages request negative IDs (-1/-2/-3); give them our real level instead.
		if (g_ddMode && id < 0 && g_ddLevelID > 0) {
			id = g_ddLevelID;
			dailyID = g_ddDailyID;
		}
		GameLevelManager::downloadLevel(id, gauntlet, dailyID);
	}
};

// ---- "Set as Daily Demon" button on level pages (whitelisted users only) --------------------

class $modify(DDLevelInfo, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge)) return false;
		if (!level || level->m_levelID.value() <= 0) return true;
		if (level->m_dailyID.value() >= DD_ID_OFFSET) {
			std::vector<CCLabelBMFont*> labels;
			collectLabels(this, labels);
			for (auto lbl : labels) {
				if (lower(lbl->getString()).find("(event)") != std::string::npos) lbl->setString("(Daily Demon)");
			}
		}
		Ref<DDLevelInfo> self = this;
		checkAllowed([self](bool ok) {
			if (!ok) return;
			queueInMainThread([self] { self->addDDButton(); });
		});
		return true;
	}

	void addDDButton() {
		if (!this->getParent() || this->getChildByID("dd-menu")) return;
		auto menu = CCMenu::create();
		menu->setID("dd-menu");
		menu->setPosition({0.f, 0.f});
		auto spr = ButtonSprite::create("Set DD", "goldFont.fnt", "GJ_button_04.png", .6f);
		spr->setScale(.7f);
		auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(DDLevelInfo::onSetDD));
		btn->setID("set-daily-demon-button");
		auto win = CCDirector::get()->getWinSize();
		btn->setPosition({win.width - 40.f, 34.f});
		menu->addChild(btn);
		this->addChild(menu, 10);
	}

	void onSetDD(CCObject*) {
		int id = m_level->m_levelID.value();
		createQuickPopup(
			"Daily Demon",
			fmt::format("Set <cy>{}</c> as today's Daily Demon?", std::string(m_level->m_levelName)),
			"Cancel", "Set",
			[id](FLAlertLayer*, bool yes) {
				if (!yes) return;
				auto req = web::WebRequest();
				req.timeout(std::chrono::seconds(10));
				req.header("Content-Type", "application/x-www-form-urlencoded");
				req.bodyString(fmt::format("userID={}&levelID={}", myUserID(), id));
				static async::TaskHolder<web::WebResponse> holder;
				holder.spawn(req.post(serverURL("setGJDDLevel.php")), [](web::WebResponse res) {
					auto body = res.string().unwrapOr("");
					if (res.ok() && body == "1") {
						Notification::create("Daily Demon set!", NotificationIcon::Success)->show();
					} else if (body == "-2") {
						Notification::create("Not authorised", NotificationIcon::Error)->show();
					} else {
						Notification::create(fmt::format("Failed ({})", body.empty() ? "no response" : body), NotificationIcon::Error)->show();
					}
				});
			}
		);
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
				auto bg = CCSprite::create(Mod::get()->expandSpriteName("DD_btn_001.png").c_str());
				if (!bg) return;
				bg->setScale((oldSpr->getContentSize().width * oldSpr->getScale()) / bg->getContentSize().width);
				auto size = bg->getContentSize();
				// Plain Easy Demon face. Sized relative to the tile (the tile PNG has no HD/UHD suffix,
				// so its size in game units is not its pixel size) and kept above the lettering.
				if (auto face = CCSprite::createWithSpriteFrameName("diffIcon_07_btn_001.png")) {
					face->setScale(size.width * .60f / face->getContentSize().width);
					face->setPosition({size.width / 2.f, size.height * .60f});
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
		beginDDMode();
		DailyLevelPage::create(GJTimedLevelType::Event)->show();
	}
};
