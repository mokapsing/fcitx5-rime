/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_RIMEENGINE_H_
#define _FCITX_RIMEENGINE_H_

#include "rimesession.h"
#include "rimestate.h"
#include <cstdint>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/handlertable_details.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/library.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/icontheme.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>
#include <list>
#include <memory>
#include <rime_api.h>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef FCITX_RIME_NO_DBUS
#include "rimeservice.h"
#endif

namespace fcitx::rime {

class RimeState;
class RimeOptionAction;

enum class SharedStatePolicy { FollowGlobalConfig, All, Program, No };

FCITX_CONFIG_ENUM_NAME_WITH_I18N(SharedStatePolicy,
                                 N_("Follow Global Configuration"), N_("All"),
                                 N_("Program"), N_("No"));

enum class PreeditMode { No, ComposingText, CommitPreview };

FCITX_CONFIG_ENUM_NAME_WITH_I18N(PreeditMode, N_("Do not show"),
                                 N_("Composing text"), N_("Commit preview"))

enum class SwitchInputMethodBehavior {
    Clear,
    CommitRawInput,
    CommitComposingText,
    CommitCommitPreview
};

FCITX_CONFIG_ENUM_NAME_WITH_I18N(SwitchInputMethodBehavior, N_("Clear"),
                                 N_("Commit raw input"),
                                 N_("Commit composing text"),
                                 N_("Commit commit preview"))

FCITX_CONFIGURATION(
    RimeEngineConfig,
    OptionWithAnnotation<PreeditMode, PreeditModeI18NAnnotation> preeditMode{
        this, "PreeditMode", _("Preedit Mode"),
        isAndroid() ? PreeditMode::No : PreeditMode::ComposingText};
    OptionWithAnnotation<SharedStatePolicy, SharedStatePolicyI18NAnnotation>
        sharedStatePolicy{this, "InputState", _("Shared Input State"),
                          SharedStatePolicy::All};
    // On Linux only cursor position is available so this pins candidate window
    // while typing. On macOS any position within embedded preedit is available
    // so this is unnecessary. On Android there is no candidate window yet.
    Option<bool> preeditCursorPositionAtBeginning{
        this, "PreeditCursorPositionAtBeginning",
        _("Fix embedded preedit cursor at the beginning of the preedit"),
        !isAndroid() && !isApple() && !isEmscripten()};
    OptionWithAnnotation<SwitchInputMethodBehavior,
                         SwitchInputMethodBehaviorI18NAnnotation>
        switchInputMethodBehavior{
            this, "SwitchInputMethodBehavior",
            _("Action when switching input method"),
            SwitchInputMethodBehavior::CommitCommitPreview};
    ExternalOption userDataDir{
        this, "UserDataDir", _("User data dir"),
        stringutils::concat(
            "xdg-open \"",
            stringutils::replaceAll((StandardPaths::global().userDirectory(
                                         StandardPathsType::PkgData) /
                                     "rime")
                                        .string(),
                                    "\"", "\"\"\""),
            "\"")};
    fcitx::Option<fcitx::KeyList> deploy{
        this, "Deploy", _("Deploy"),
        isApple() ? fcitx::KeyList{fcitx::Key("Control+Alt+grave")}
                  : fcitx::KeyList{}};
    fcitx::Option<fcitx::KeyList> synchronize{
        this, "Synchronize", _("Synchronize"), {}};);

class RimeEngine final : public InputMethodEngineV2 {
public:
    RimeEngine(Instance *instance);
    ~RimeEngine();
    Instance *instance() { return instance_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void save() override;
    auto &factory() { return factory_; }

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        safeSaveAsIni(config_, "conf/rime.conf");
        updateConfig();
    }
    void setSubConfig(const std::string &path,
                      const RawConfig & /*unused*/) override;
    void updateConfig();

    std::string subMode(const InputMethodEntry & /*entry*/,
                        InputContext & /*inputContext*/) override;
    std::string subModeIconImpl(const InputMethodEntry & /*unused*/,
                                InputContext & /*unused*/) override;
    std::string subModeLabelImpl(const InputMethodEntry & /*unused*/,
                                 InputContext & /*unused*/) override;
    const RimeEngineConfig &config() const { return config_; }

    rime_api_t *api() { return api_; }
    const auto &appOptions() const { return appOptions_; }

    void rimeStart(bool fullcheck);

    RimeState *state(InputContext *ic);
    RimeSessionPool &sessionPool() { return sessionPool_; }

#ifndef FCITX_RIME_NO_DBUS
    FCITX_ADDON_DEPENDENCY_LOADER(dbus, instance_->addonManager());
#endif

    void allowNotification(std::string type = "");
    const auto &schemas() const { return schemas_; }
    const auto &optionActions() const { return optionActions_; };

private:
    static void rimeNotificationHandler(void *context, RimeSessionId session,
                                        const char *messageTypee,
                                        const char *messageValue);

    void deploy();
    void sync(bool userTriggered);
    void updateSchemaMenu();
    void updateActionsForSchema(const std::string &schema);
    void notifyImmediately(RimeSessionId session, std::string_view type,
                           std::string_view value);
    void notify(RimeSessionId session, const std::string &type,
                const std::string &value);
    void releaseAllSession(bool snapshot = false);
    void updateAppOptions();
    void refreshStatusArea(InputContext &ic);
    void refreshStatusArea(RimeSessionId session);
    void updateStatusArea(RimeSessionId session);
    void refreshSessionPoolPolicy();
    PropertyPropagatePolicy getSharedStatePolicy();

    bool constructed_ = false;
    std::string sharedDataDir_;
    IconTheme theme_;
    Instance *instance_;
    EventDispatcher eventDispatcher_;
    rime_api_t *api_;
    static bool firstRun_;
    uint64_t silenceNotificationUntil_ = 0;
    uint64_t allowNotificationUntil_ = 0;
    std::string allowNotificationType_;
    FactoryFor<RimeState> factory_;
    bool needRefreshAppOption_ = false;

    std::unique_ptr<Action> imAction_;
    SimpleAction separatorAction_;
    SimpleAction deployAction_;
    SimpleAction syncAction_;

    RimeEngineConfig config_;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>>
        appOptions_;

    FCITX_ADDON_DEPENDENCY_LOADER(notifications, instance_->addonManager());

    std::vector<std::string> schemas_;
    std::list<SimpleAction> schemActions_;
    std::unordered_map<std::string,
                       std::list<std::unique_ptr<RimeOptionAction>>>
        optionActions_;
    Menu schemaMenu_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> globalConfigReloadHandle_;

#ifndef FCITX_RIME_NO_DBUS
    RimeService service_{this};
#endif
    RimeSessionPool sessionPool_;
    std::thread::id mainThreadId_ = std::this_thread::get_id();
    RimeState *currentKeyEventState_ = nullptr;
};
} // namespace fcitx::rime

FCITX_DECLARE_LOG_CATEGORY(rime_log);

#define RIME_DEBUG() FCITX_LOGC(rime_log, Debug)
#define RIME_ERROR() FCITX_LOGC(rime_log, Error)

#endif // _FCITX_RIMEENGINE_H_
