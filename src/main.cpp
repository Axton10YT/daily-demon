// Hourly Level - replaces the Map Pack button in CreatorLayer with an Hourly Level button.
//
// The popup is the game's own DailyLevelPage opened as the Event type, so it looks and behaves
// like the Event level popup. While that popup is open ("HL mode") three calls are redirected:
//   GameLevelManager::getGJDailyLevelState(Event) -> GET {server}/getGJHourlyLevel.php  ("hourID|secondsLeft|levelID|number")
//   GameLevelManager::downloadLevel(<negative id>, .., dailyID) -> downloads the real levelID from the endpoint
//   DailyLevelPage::levelDownloadFinished          -> tags the level with a daily ID so the node is built
//
// Setting the Hourly Level: a lock button on level pages asks for a PIN (checked by validateGJHLPIN.php),
// then offers "set as Hourly Level". The set request carries the PIN again plus the player's accountID and
// userID, and the server checks them against its whitelist of allowed account IDs (setGJHLLevel.php).
//
// Geode v6 port notes:
//  - uses the same server endpoints as Daily Demon (the server decides when the level changes)
//  - needs C++23 and Geode >= 6.0.0-alpha.1 (GD 2.2091)
//  - gd::string is gone in v6, so nothing here relies on it (the old moderator-popup hook that took
//    gd::string arguments belonged to Daily Demon and was dropped)

#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/DailyLevelPage.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/ui/TextInput.hpp>
#include <ctime>
#include <set>

using namespace geode::prelude;

namespace {
	// Offset keeps our daily ID clear of any real Event IDs stored by the game (and of Daily Demon's 700000+).
	constexpr int HL_ID_OFFSET = 900000;

	bool g_hlMode = false;
	int g_hlLevelID = 0;
	int g_hlDailyID = 0;
	int g_hlNumber = 0;          // "Hourly Level #N"
	std::time_t g_hlEndsAt = 0;  // when the current hourly level expires (unix time)
	int g_hlHourID = 0;          // period ID the server returned for the shown level
	int g_hlWant = 0;            // >0: browse to this numbered Hourly Level instead of the current one

	// Web requests are futures; a TaskHolder aborts the task when replaced/destroyed
	// and runs the callback on the main thread.
	async::TaskHolder<web::WebResponse> g_fetchHolder;

	std::string serverBase() {
		auto base = Mod::get()->getSettingValue<std::string>("server");
		while (!base.empty() && base.back() == '/') base.pop_back();
		return base;
	}

	std::string serverURL(std::string const& path) {
		return serverBase() + "/" + path;
	}

	// ---- level setters: PIN -> menu -> set ------------------------------------------------------

	std::string g_hlPin;  // kept for this game session once the server has accepted it
	async::TaskHolder<web::WebResponse> g_pinHolder;
	async::TaskHolder<web::WebResponse> g_setHolder;

	web::WebRequest formPost(std::string const& body) {
		auto req = web::WebRequest();
		req.timeout(std::chrono::seconds(10));
		req.header("Content-Type", "application/x-www-form-urlencoded");
		req.bodyString(body);
		return req;
	}

	void setHourlyLevel(int levelID) {
		int accountID = GJAccountManager::get()->m_accountID;
		int userID = GameManager::get()->m_playerUserID.value();
		if (accountID <= 0 || userID <= 0) {
			return Notification::create("Log into your GD account first", NotificationIcon::Error)->show();
		}
		g_setHolder.spawn(
			formPost(fmt::format("pin={}&accountID={}&userID={}&levelID={}", g_hlPin, accountID, userID, levelID)).post(serverURL("setGJHLLevel.php")),
			[](web::WebResponse res) {
				auto body = res.string().unwrapOr("");
				if (res.ok() && body == "1") {
					Notification::create("Hourly Level set!", NotificationIcon::Success)->show();
				} else if (body == "-2") {
					Notification::create("Your account isn't allowed to set Hourly Levels", NotificationIcon::Error)->show();
				} else if (body == "-6") {
					g_hlPin.clear(); // the PIN changed on the server: ask again next time
					Notification::create("PIN rejected, unlock again", NotificationIcon::Error)->show();
				} else if (body == "-7") {
					Notification::create("Account and user ID don't match", NotificationIcon::Error)->show();
				} else if (body == "2") {
					Notification::create("Too many attempts, try again later", NotificationIcon::Error)->show();
				} else {
					Notification::create(fmt::format("Failed ({})", body.empty() ? "no response" : body), NotificationIcon::Error)->show();
				}
			}
		);
	}

	// The menu you get once the PIN has been accepted.
	void showSetMenu(int levelID, std::string name) {
		createQuickPopup(
			"Hourly Level",
			fmt::format("<cy>{}</c> ({})\n\nSet this level as the current Hourly Level?", name, levelID),
			"Cancel", "Set",
			[levelID](FLAlertLayer*, bool yes) {
				if (yes) setHourlyLevel(levelID);
			}
		);
	}

	void promptPin(int levelID, std::string name) {
		auto input = TextInput::create(180.f, "PIN", "bigFont.fnt");
		input->setFilter("0123456789");
		input->setMaxCharCount(12);
		input->setPasswordMode(true);
		Ref<TextInput> inputRef = input;
		auto alert = createQuickPopup(
			"Hourly Level",
			"Enter the PIN to set Hourly Levels:\n\n\n",
			"Cancel", "Unlock", 320.f,
			[inputRef, levelID, name](FLAlertLayer*, bool yes) {
				if (!yes) return;
				std::string pin = inputRef->getString();
				if (pin.empty()) return;
				g_pinHolder.spawn(
					formPost(fmt::format("pin={}", pin)).post(serverURL("validateGJHLPIN.php")),
					[pin, levelID, name](web::WebResponse res) {
						auto body = res.string().unwrapOr("");
						if (res.ok() && body == "1") {
							g_hlPin = pin;
							showSetMenu(levelID, name);
						} else if (body == "2") {
							Notification::create("Too many attempts, try again later", NotificationIcon::Error)->show();
						} else {
							Notification::create("Wrong PIN", NotificationIcon::Error)->show();
						}
					}
				);
			},
			false
		);
		auto win = CCDirector::get()->getWinSize();
		input->setPosition({win.width / 2.f, win.height / 2.f - 8.f});
		alert->m_mainLayer->addChild(input);
		handleTouchPriority(alert);
		alert->show();
	}

	// The popup borrows the game's Event slot. Stash the real Event state while it is open
	// (so cached Event data never shows up here, and ours never leaks into the real Event button).
	struct SavedEvent { int id = 0, timeLeft = 0, active = 0; bool has = false; } g_saved;

	void beginHLMode() {
		auto mgr = GameLevelManager::sharedState();
		if (!g_saved.has) {
			g_saved = { mgr->m_eventID, mgr->m_eventTimeLeft, mgr->m_activeEventID, true };
		}
		mgr->m_eventID = 0;
		mgr->m_eventTimeLeft = 0;
		mgr->m_activeEventID = 0;
		g_hlLevelID = 0;
		g_hlDailyID = 0;
		g_hlNumber = 0;
		g_hlEndsAt = 0;
		g_hlHourID = 0;
		g_hlWant = 0;
		g_hlMode = true;
	}

	// Entries we put into the manager's daily-level dictionary. They must never survive into the
	// game's save data, so they are removed when the popup closes and right before every save.
	std::set<int> g_injected;

	// Removes every trace of our synthetic daily entries from the manager (anything with a daily ID
	// at or above our offset, including ones an interrupted session may have left in the save file).
	void purgeInjected() {
		auto mgr = GameLevelManager::sharedState();
		if (!mgr) return;
		g_injected.clear();
		if (auto dict = mgr->m_dailyLevels) {
			// Works for both int- and string-keyed dictionaries.
			if (auto keys = dict->allKeys()) {
				for (auto key : CCArrayExt<CCObject*>(keys)) {
					bool isInt = false;
					int intKey = 0;
					std::string strKey;
					CCObject* obj = nullptr;
					if (auto i = typeinfo_cast<CCInteger*>(key)) {
						isInt = true;
						intKey = i->getValue();
						obj = dict->objectForKey(intKey);
					} else if (auto str = typeinfo_cast<CCString*>(key)) {
						strKey = str->getCString();
						obj = dict->objectForKey(strKey);
					} else {
						continue;
					}
					auto lvl = static_cast<GJGameLevel*>(obj);
					if (!lvl || lvl->m_dailyID.value() >= HL_ID_OFFSET) {
						if (lvl) lvl->m_dailyID = 0;
						if (isInt) dict->removeObjectForKey(intKey);
						else dict->removeObjectForKey(strKey);
					}
				}
			}
		}
	}

	void restoreEventState() {
		auto mgr = GameLevelManager::sharedState();
		if (!mgr) return;
		if (g_saved.has) {
			mgr->m_eventID = g_saved.id;
			mgr->m_eventTimeLeft = g_saved.timeLeft;
			mgr->m_activeEventID = g_saved.active;
			g_saved.has = false;
		}
		// An Event slot still holding one of our IDs points at a level that doesn't exist;
		// reset it so the game never looks it up.
		if (mgr->m_eventID >= HL_ID_OFFSET || mgr->m_activeEventID >= HL_ID_OFFSET) {
			mgr->m_eventID = 0;
			mgr->m_eventTimeLeft = 0;
			mgr->m_activeEventID = 0;
		}
	}

	void endHLMode() {
		g_hlMode = false;
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
		// Browsing an older numbered level: no countdown, just which one it is.
		if (g_hlWant > 0) return fmt::format("Hourly Level #{}", g_hlWant);
		long left = static_cast<long>(g_hlEndsAt - std::time(nullptr));
		if (left < 0) left = 0;
		return fmt::format("New Hourly Level in: {:02}:{:02}:{:02}", left / 3600, (left / 60) % 60, left % 60);
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

	// Event pages carry a reward chest in the bottom centre. Hourly Levels have no chest.
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
	void tidyHLPage(CCNode* page, CCNode* layer = nullptr) {
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
			if (lowest && g_hlHourID > 0) lowest->setString(countdownText().c_str());
		}
		for (auto lbl : labels) {
			std::string t = lbl->getString();
			std::string l = lower(t);
			if (startsWith(t, "Event #")) {
				lbl->setString(fmt::format("Hourly Level #{}", g_hlNumber > 0 ? g_hlNumber : 1).c_str());
			} else if (startsWith(t, "Current:")) {
				lbl->setVisible(false);
			} else if (l == "bonus:") {
				lbl->setVisible(false);
				auto wp = lbl->getParent()->convertToWorldSpace(lbl->getPosition());
				hideNear(page, wp); // the reward chest sits just to the right of the label
			} else if (l.find("something epic") != std::string::npos || startsWith(t, "New Hourly Level in:")) {
				lbl->setString(countdownText().c_str());
			}
		}
	}

	void failStatus(GJErrorCode code) {
		if (code == GJErrorCode::NotFound) {
			Notification::create("No Hourly Level has been set for this hour", NotificationIcon::Info)->show();
		} else {
			Notification::create("Couldn't reach the Hourly Level server", NotificationIcon::Error)->show();
		}
		auto mgr = GameLevelManager::sharedState();
		if (mgr->m_GJDailyLevelDelegate) mgr->m_GJDailyLevelDelegate->dailyStatusFailed(GJTimedLevelType::Event, code);
	}

	void fetchHourlyLevel() {
		g_fetchHolder.spawn(
			web::WebRequest().timeout(std::chrono::seconds(10)).get(serverURL(g_hlWant > 0 ? fmt::format("getGJHourlyLevel.php?number={}", g_hlWant) : std::string("getGJHourlyLevel.php"))),
			[](web::WebResponse res) {
				if (!g_hlMode) return;
				if (!res.ok()) return failStatus(GJErrorCode::GenericError);

				auto body = res.string().unwrapOr("");
				auto parts = utils::string::split(body, "|");
				if (parts.size() < 3) return failStatus(GJErrorCode::NotFound); // "-1" = nothing set for this hour

				auto hour = utils::numFromString<int>(parts[0]);
				auto left = utils::numFromString<int>(parts[1]);
				auto lvl = utils::numFromString<int>(parts[2]);
				if (hour.isErr() || left.isErr() || lvl.isErr() || lvl.unwrap() <= 0) return failStatus(GJErrorCode::NotFound);

				g_hlLevelID = lvl.unwrap();
				g_hlNumber = 0;
				if (parts.size() >= 4) {
					if (auto n = utils::numFromString<int>(parts[3]); n.isOk()) g_hlNumber = n.unwrap();
				}
				g_hlEndsAt = std::time(nullptr) + left.unwrap();
				g_hlHourID = hour.unwrap();
				g_hlDailyID = HL_ID_OFFSET + (hour.unwrap() % 100000);

				auto mgr = GameLevelManager::sharedState();
				mgr->storeDailyLevelState(g_hlDailyID, g_hlWant > 0 ? 86400 : left.unwrap(), GJTimedLevelType::Event);
				if (mgr->m_GJDailyLevelDelegate) mgr->m_GJDailyLevelDelegate->dailyStatusFinished(GJTimedLevelType::Event);
			}
		);
	}
}

// Clean any leftovers once the main menu is up (before the game can save).
class $modify(HLMenu, MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) return false;
		if (!g_hlMode) {
			purgeInjected();
			restoreEventState();
		}
		return true;
	}
};

// ---- the popup itself ---------------------------------------------------------------------

class $modify(HLPage, DailyLevelPage) {
	struct Fields {
		bool m_isHL = false;
	};

	bool init(GJTimedLevelType type) {
		if (!DailyLevelPage::init(type)) return false;
		if (!g_hlMode || type != GJTimedLevelType::Event) return true;
		m_fields->m_isHL = true;

		// Swap the "EVENT" title sprite for the Hourly Level logo.
		auto frame = CCSpriteFrameCache::get()->spriteFrameByName("eventLevelLabel_001.png");
		if (frame && m_mainLayer) {
			for (auto child : CCArrayExt<CCNode*>(m_mainLayer->getChildren())) {
				auto spr = typeinfo_cast<CCSprite*>(child);
				if (spr && spr->isFrameDisplayed(frame)) {
					auto logo = CCSprite::create(Mod::get()->expandSpriteName("HL_title.png").c_str());
					if (logo && m_buttonMenu) {
						logo->setScale(150.f / logo->getContentSize().width);
						logo->setID("hl-logo");
						auto world = spr->getParent()->convertToWorldSpace(spr->getPosition());
						logo->setPosition(m_buttonMenu->convertToNodeSpace(world));
						m_buttonMenu->addChild(logo);
					}
					spr->setVisible(false);
					break;
				}
			}
		}

		// Black panel inside the popup frame (a child of the background so it draws right above it).
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
				auto panel = CCLayerColor::create({0, 0, 0, 255}, sz.width - 12.f, sz.height - 12.f);
				panel->setPosition({6.f, 6.f});
				bg->addChild(panel, 1);
			}
		}

		return true;
	}

	static CCMenuItem* findClaim(CCNode* node) {
		if (!node) return nullptr;
		for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
			if (auto item = typeinfo_cast<CCMenuItem*>(child)) {
				if (item->m_pfnSelector == menu_selector(DailyLevelNode::onClaimReward)) return item;
			}
			if (auto r = findClaim(child)) return r;
		}
		return nullptr;
	}

	// Hourly Levels have no reward to claim: swap the Claim button for "Load Next".
	void swapClaimButton() {
		auto claim = findClaim(m_dailyNode);
		if (!claim || !claim->getParent()) return;
		claim->setVisible(false);
		claim->setEnabled(false);
		if (claim->getParent()->getChildByID("hl-next-button")) return;
		auto spr = ButtonSprite::create("Load Next", "goldFont.fnt", "GJ_button_01.png", .7f);
		auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HLPage::onLoadNext));
		btn->setID("hl-next-button");
		btn->setPosition(claim->getPosition());
		claim->getParent()->addChild(btn);
	}

	void onLoadNext(CCObject*) {
		int next = (g_hlNumber > 0 ? g_hlNumber : 1) + 1;
		Ref<HLPage> self = this;
		static async::TaskHolder<web::WebResponse> holder;
		holder.spawn(
			web::WebRequest().timeout(std::chrono::seconds(10)).get(serverURL(fmt::format("getGJHourlyLevel.php?number={}", next))),
			[self, next](web::WebResponse res) {
				auto body = res.string().unwrapOr("");
				if (!res.ok() || utils::string::split(body, "|").size() < 3) {
					return Notification::create("That's the latest Hourly Level", NotificationIcon::Info)->show();
				}
				self->onClose(nullptr);   // restores the real Event state
				beginHLMode();
				g_hlWant = next;
				DailyLevelPage::create(GJTimedLevelType::Event)->show();
			}
		);
	}

	void createDailyNode(GJGameLevel* level, bool instant, float delay, bool isNew) {
		DailyLevelPage::createDailyNode(level, instant, delay, isNew);
		if (m_fields->m_isHL) { tidyHLPage(this, m_mainLayer); swapClaimButton(); }
	}

	void updateTimers(float dt) {
		DailyLevelPage::updateTimers(dt);
		if (m_fields->m_isHL) { tidyHLPage(this, m_mainLayer); swapClaimButton(); }
	}

	void onClose(CCObject* sender) {
		if (m_fields->m_isHL) endHLMode();
		DailyLevelPage::onClose(sender);
	}

	void onTheSafe(CCObject* sender) {
		if (!m_fields->m_isHL) return DailyLevelPage::onTheSafe(sender);
		// The Safe lists every level that was ever an Hourly Level.
		Ref<HLPage> self = this;
		static async::TaskHolder<web::WebResponse> holder;
		holder.spawn(
			web::WebRequest().timeout(std::chrono::seconds(10)).get(serverURL("getGJHLList.php")),
			[self](web::WebResponse res) {
				auto body = res.string().unwrapOr("");
				if (!res.ok() || body.empty() || body == "-1") {
					return Notification::create("No Hourly Levels yet", NotificationIcon::Info)->show();
				}
				auto search = GJSearchObject::create(SearchType::MapPackOnClick, body);
				CCDirector::get()->pushScene(CCTransitionFade::create(.5f, LevelBrowserLayer::scene(search)));
			}
		);
	}

	void keyBackClicked() {
		if (m_fields->m_isHL) endHLMode();
		DailyLevelPage::keyBackClicked();
	}

	void levelDownloadFinished(GJGameLevel* level) {
		if (m_fields->m_isHL) {
			log::info("Hourly Level: levelDownloadFinished id={} dailyID={} (page wants {}, started={})",
				level ? level->m_levelID.value() : -1, level ? level->m_dailyID.value() : -1, m_downloadLevelID, m_downloadStarted);
		}
		if (m_fields->m_isHL && level && g_hlDailyID > 0) {
			// The page only builds its node for levels carrying a daily ID, and 2.2091 also checks the
			// level it was waiting for: tell it the real one arrived.
			level->m_dailyID = g_hlDailyID;
			m_downloadLevelID = level->m_levelID.value();
			if (auto mgr = GameLevelManager::sharedState(); mgr->m_dailyLevels) {
				mgr->m_dailyLevels->setObject(level, g_hlDailyID);
			}
		}
		DailyLevelPage::levelDownloadFinished(level);
	}

	void levelDownloadFailed(int response) {
		if (m_fields->m_isHL) {
			log::warn("Hourly Level: level download failed (arg {}), wanted level {}", response, g_hlLevelID);
			Notification::create(fmt::format("Level {} couldn't be downloaded - it may be deleted or not exist", g_hlLevelID), NotificationIcon::Error)->show();
		}
		DailyLevelPage::levelDownloadFailed(response);
	}
};

class $modify(HLManager, GameLevelManager) {
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
		if (g_hlMode && type == GJTimedLevelType::Event) {
			fetchHourlyLevel();
			return true;
		}
		return GameLevelManager::getGJDailyLevelState(type);
	}

	void downloadLevel(int id, bool gauntlet, int dailyID) {
		// While the popup is open every download it makes should be our real level. 2.2081 asked for the
		// negative -1/-2/-3 IDs; 2.2091's page tracks its own download ID, so match on any of: a negative
		// ID, our synthetic daily ID used as the level ID, or our daily ID passed along.
		if (g_hlMode && g_hlLevelID > 0 && id != g_hlLevelID && (id < 0 || id == g_hlDailyID || dailyID == g_hlDailyID)) {
			log::info("Hourly Level: redirecting download {} (daily {}) -> level {}", id, dailyID, g_hlLevelID);
			id = g_hlLevelID;
			dailyID = g_hlDailyID;
		} else if (g_hlMode) {
			log::info("Hourly Level: downloadLevel({}, {}, {}) left alone (level {}, daily {})", id, gauntlet, dailyID, g_hlLevelID, g_hlDailyID);
		}
		GameLevelManager::downloadLevel(id, gauntlet, dailyID);
	}
};

// ---- level pages: "(Event)" -> "(Hourly Level)" and the lock button for level setters ------------

class $modify(HLLevelInfo, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge)) return false;
		if (!level) return true;
		if (level->m_dailyID.value() >= HL_ID_OFFSET) {
			std::vector<CCLabelBMFont*> labels;
			collectLabels(this, labels);
			for (auto lbl : labels) {
				if (lower(lbl->getString()).find("(event)") != std::string::npos) lbl->setString("(Hourly Level)");
			}
		}
		if (level->m_levelID.value() > 0 && !serverBase().empty()) this->addLockButton();
		return true;
	}

	void addLockButton() {
		auto menu = CCMenu::create();
		menu->setID("hl-menu");
		menu->setPosition({0.f, 0.f});
		// The game's own lock icon, so it matches the rest of the UI.
		CCNode* spr = CCSprite::createWithSpriteFrameName("GJ_lock_001.png");
		if (spr) {
			spr->setScale(.9f);
		} else {
			spr = ButtonSprite::create("Lock", "goldFont.fnt", "GJ_button_04.png", .6f);
		}
		auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HLLevelInfo::onLock));
		btn->setID("hourly-level-lock-button");
		auto win = CCDirector::get()->getWinSize();
		btn->setPosition({win.width - 30.f, 30.f});
		menu->addChild(btn);
		this->addChild(menu, 10);
	}

	void onLock(CCObject*) {
		int id = m_level->m_levelID.value();
		std::string name = m_level->m_levelName;
		if (g_hlPin.empty()) promptPin(id, name);
		else showSetMenu(id, name);
	}
};

// ---- Map Pack -> Hourly Level -------------------------------------------------------------

class $modify(HLCreatorLayer, CreatorLayer) {
	bool init() {
		if (!CreatorLayer::init()) return false;
		this->replaceMapPackButton();
		return true;
	}

	// Geode's node IDs first, then fall back to hunting for the vanilla sprite frame.
	CCMenuItemSpriteExtra* findMapPackButton() {
		if (auto byID = typeinfo_cast<CCMenuItemSpriteExtra*>(this->getChildByIDRecursive("map-packs-button"))) {
			return byID;
		}
		auto frame = CCSpriteFrameCache::get()->spriteFrameByName("GJ_mapPacksBtn_001.png");
		if (!frame) return nullptr;
		for (auto node : CCArrayExt<CCNode*>(this->getChildren())) {
			auto menu = typeinfo_cast<CCMenu*>(node);
			if (!menu) continue;
			for (auto item : CCArrayExt<CCNode*>(menu->getChildren())) {
				auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(item);
				if (!btn) continue;
				auto spr = typeinfo_cast<CCSprite*>(btn->getNormalImage());
				if (spr && spr->isFrameDisplayed(frame)) return btn;
			}
		}
		return nullptr;
	}

	void replaceMapPackButton() {
		auto btn = this->findMapPackButton();
		if (!btn || !btn->getParent()) {
			log::warn("Map Pack button not found in CreatorLayer; Hourly Level button not added");
			return;
		}
		auto oldImg = btn->getNormalImage();

		// The tile PNG has no HD/UHD suffix, so scale it to the size the old tile had on screen.
		auto tile = CCSprite::create(Mod::get()->expandSpriteName("HL_btn_001.png").c_str());
		if (!tile) return;
		tile->setScale((oldImg->getContentSize().width * oldImg->getScale()) / tile->getContentSize().width);

		// Icon: the game's own clock sprite (the one level cells use for length), so it matches every other
		// GD icon. If this GD build doesn't have that frame, fall back to the bundled clock.
		auto size = tile->getContentSize();
		CCSprite* icon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
		float iconWidth = .5f;
		if (!icon) {
			icon = CCSprite::create(Mod::get()->expandSpriteName("HL_clock.png").c_str());
			iconWidth = .58f;
		}
		if (icon) {
			icon->setScale(size.width * iconWidth / icon->getContentSize().width);
			icon->setPosition({size.width * .472f, size.height * .53f}); // tile art is 94/199 across; the rest is its drop shadow
			tile->addChild(icon);
		}

		auto newBtn = CCMenuItemSpriteExtra::create(tile, this, menu_selector(HLCreatorLayer::onHourlyLevel));
		newBtn->setID("hourly-level-button");
		newBtn->setScale(btn->getScale());
		newBtn->m_baseScale = btn->m_baseScale; // keep the same size as the neighbouring tiles
		newBtn->setPosition(btn->getPosition());
		newBtn->setZOrder(btn->getZOrder());
		btn->getParent()->addChild(newBtn);
		btn->removeFromParent();
	}

	void onHourlyLevel(CCObject*) {
		if (serverBase().empty()) {
			return Notification::create("Set the Hourly Level server in the mod's settings first", NotificationIcon::Info)->show();
		}
		beginHLMode();
		DailyLevelPage::create(GJTimedLevelType::Event)->show();
	}
};
