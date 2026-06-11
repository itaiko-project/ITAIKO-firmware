#include "utils/Menu.h"

#include "peripherals/Drum.h"

namespace Doncon::Utils {

// NOLINTBEGIN(modernize-use-designated-initializers)
const std::map<Menu::Page, const Menu::Descriptor> Menu::descriptors = {
    {Menu::Page::Main,                                            //
     {Menu::Descriptor::Type::Menu,                               //
      "Settings",                                                 //
      {{"Mode", Menu::Descriptor::Action::GotoPageDeviceMode},    //
       {"Drum", Menu::Descriptor::Action::GotoPageDrum},          //
       {"Led", Menu::Descriptor::Action::GotoPageLed},            //
       {"Reset", Menu::Descriptor::Action::GotoPageReset},        //
       {"Macro", Menu::Descriptor::Action::GotoPageClearMacro},   //
       {"USB Flash", Menu::Descriptor::Action::GotoPageBootsel},  //
       {"Version", Menu::Descriptor::Action::GotoPageVersion},     //
       {"PS4 Key", Menu::Descriptor::Action::GotoPagePS4Auth},    //
       {"PS3 MAC", Menu::Descriptor::Action::GotoPageMacAddress}},//
      0}},                                                        //

    {Menu::Page::Version,                            //
     {Menu::Descriptor::Type::Info,                  //
      "Firmware Version",                            //
      {{FIRMWARE_VERSION, Menu::Descriptor::Action::None}}, //
      0}},                                           //

    {Menu::Page::PS4Auth,                                      //
     {Menu::Descriptor::Type::Info,                            //
      "PS4 Auth Key",                                          //
      {{"None", Menu::Descriptor::Action::None},               //
       {"Present", Menu::Descriptor::Action::None}},           //
      0}},                                                     //

    {Menu::Page::MacAddress,                                   //
     {Menu::Descriptor::Type::Info,                            //
      "PS3 MAC",                                               //
      {{"", Menu::Descriptor::Action::None}},                  //
      0}},                                                     //

    {Menu::Page::DeviceMode,                                  //
     {Menu::Descriptor::Type::Selection,                      //
      "Mode",                                                 //
      {{"Keybrd P1", Menu::Descriptor::Action::SetUsbMode},   //
       {"Keybrd P2", Menu::Descriptor::Action::SetUsbMode},   //
       {"Swtch Tata", Menu::Descriptor::Action::SetUsbMode},  //
       {"PS4 Tata", Menu::Descriptor::Action::SetUsbMode},    //
       {"Joystick", Menu::Descriptor::Action::SetUsbMode},    //
       {"USIO Taiko", Menu::Descriptor::Action::SetUsbMode},  //
       {"MIDI", Menu::Descriptor::Action::SetUsbMode},        //
       {"Dualshock3", Menu::Descriptor::Action::SetUsbMode}}, //
      0}},                                                    //

    {Menu::Page::Drum,                                                           //
     {Menu::Descriptor::Type::Menu,                                              //
      "Drum Settings",                                                           //
      {{"Hold Time", Menu::Descriptor::Action::GotoPageDrumDebounceDelay},       //
       {"Debounce", Menu::Descriptor::Action::GotoPageDrumDebounce},             //
       {"Thresholds", Menu::Descriptor::Action::GotoPageDrumTriggerThresholds},  //
       {"Cutoff", Menu::Descriptor::Action::GotoPageDrumCutoffThresholds},       //
       {"Double Trg", Menu::Descriptor::Action::GotoPageDrumDoubleTrigger}},     //
      0}},                                                                       //

    {Menu::Page::DrumTriggerThresholds,                                               //
     {Menu::Descriptor::Type::Menu,                                                   //
      "Thresholds",                                                                   //
      {{"Left Ka", Menu::Descriptor::Action::GotoPageDrumTriggerThresholdKaLeft},     //
       {"Left Don", Menu::Descriptor::Action::GotoPageDrumTriggerThresholdDonLeft},   //
       {"Right Don", Menu::Descriptor::Action::GotoPageDrumTriggerThresholdDonRight}, //
       {"Right Ka", Menu::Descriptor::Action::GotoPageDrumTriggerThresholdKaRight}},  //
      0}},                                                                            //

    {Menu::Page::DrumCutoffThresholds,                                              //
     {Menu::Descriptor::Type::Menu,                                                 //
      "Cutoff",                                                                     //
      {{"Left Ka", Menu::Descriptor::Action::GotoPageDrumCutoffThresholdKaLeft},    //
       {"Left Don", Menu::Descriptor::Action::GotoPageDrumCutoffThresholdDonLeft},  //
       {"Right Don", Menu::Descriptor::Action::GotoPageDrumCutoffThresholdDonRight}, //
       {"Right Ka", Menu::Descriptor::Action::GotoPageDrumCutoffThresholdKaRight}}, //
      0}},                                                                          //

    {Menu::Page::DrumDoubleTrigger,                                                  //
     {Menu::Descriptor::Type::Menu,                                                  //
      "Double Hit Mode",                                                             //
      {{"Off", Menu::Descriptor::Action::SetDoubleTriggerOff},                       //
       {"Threshold", Menu::Descriptor::Action::GotoPageDrumDoubleTriggerThresholds}}, //
      0}},                                                                           //

    {Menu::Page::DrumDoubleTriggerThresholds,                                               //
     {Menu::Descriptor::Type::Menu,                                                         //
      "Double Thresholds",                                                                  //
      {{"Left Ka", Menu::Descriptor::Action::GotoPageDrumDoubleTriggerThresholdKaLeft},     //
       {"Left Don", Menu::Descriptor::Action::GotoPageDrumDoubleTriggerThresholdDonLeft},   //
       {"Right Don", Menu::Descriptor::Action::GotoPageDrumDoubleTriggerThresholdDonRight}, //
       {"Right Ka", Menu::Descriptor::Action::GotoPageDrumDoubleTriggerThresholdKaRight}},  //
      0}},

    {Menu::Page::DrumDebounceDelay,                           //
     {Menu::Descriptor::Type::Value,                          //
      "Hit Hold Time (ms)",                                   //
      {{"", Menu::Descriptor::Action::SetDrumDebounceDelay}}, //
      UINT8_MAX}},

    {Menu::Page::DrumKeyTimeout,                           //
     {Menu::Descriptor::Type::Value,                       //
      "Key Debounce (ms)",                                 //
      {{"", Menu::Descriptor::Action::SetDrumKeyTimeout}}, //
      UINT8_MAX}},

    {Menu::Page::DrumDebounce,                                         //
     {Menu::Descriptor::Type::Menu,                                    //
      "Debounce Settings",                                             //
      {{"Don", Menu::Descriptor::Action::GotoPageDrumDonDebounce},     //
       {"Ka", Menu::Descriptor::Action::GotoPageDrumKatDebounce},      //
       {"Crosstalk", Menu::Descriptor::Action::GotoPageDrumCrosstalkDebounce}, //
       {"Key", Menu::Descriptor::Action::GotoPageDrumKeyTimeout}},     //
      0}},

    {Menu::Page::DrumDonDebounce,                           //
     {Menu::Descriptor::Type::Value,                        //
      "Don Debounce (ms)",                                  //
      {{"", Menu::Descriptor::Action::SetDrumDonDebounce}}, //
      UINT8_MAX}},

    {Menu::Page::DrumKatDebounce,                           //
     {Menu::Descriptor::Type::Value,                        //
      "Ka Debounce (ms)",                                   //
      {{"", Menu::Descriptor::Action::SetDrumKatDebounce}}, //
      UINT8_MAX}},

    {Menu::Page::DrumCrosstalkDebounce,                           //
     {Menu::Descriptor::Type::Value,                              //
      "Crosstalk Debounce (ms)",                                  //
      {{"", Menu::Descriptor::Action::SetDrumCrosstalkDebounce}}, //
      UINT8_MAX}},

    {Menu::Page::DrumTriggerThresholdKaLeft,                           //
     {Menu::Descriptor::Type::Value,                                   //
      "Trg Level Left Ka",                                             //
      {{"", Menu::Descriptor::Action::SetDrumTriggerThresholdKaLeft}}, //
      4095}},

    {Menu::Page::DrumTriggerThresholdDonLeft,                           //
     {Menu::Descriptor::Type::Value,                                    //
      "Trg Level Left Don",                                             //
      {{"", Menu::Descriptor::Action::SetDrumTriggerThresholdDonLeft}}, //
      4095}},

    {Menu::Page::DrumTriggerThresholdDonRight,                           //
     {Menu::Descriptor::Type::Value,                                     //
      "Trg Level Right Don",                                             //
      {{"", Menu::Descriptor::Action::SetDrumTriggerThresholdDonRight}}, //
      4095}},

    {Menu::Page::DrumTriggerThresholdKaRight,                           //
     {Menu::Descriptor::Type::Value,                                    //
      "Trg Level Right Ka",                                             //
      {{"", Menu::Descriptor::Action::SetDrumTriggerThresholdKaRight}}, //
      4095}},

    {Menu::Page::DrumDoubleTriggerThresholdKaLeft,                           //
     {Menu::Descriptor::Type::Value,                                         //
      "Trg Level Left Ka",                                                   //
      {{"", Menu::Descriptor::Action::SetDrumDoubleTriggerThresholdKaLeft}}, //
      4095}},

    {Menu::Page::DrumDoubleTriggerThresholdDonLeft,                           //
     {Menu::Descriptor::Type::Value,                                          //
      "Trg Level Left Don",                                                   //
      {{"", Menu::Descriptor::Action::SetDrumDoubleTriggerThresholdDonLeft}}, //
      4095}},

    {Menu::Page::DrumDoubleTriggerThresholdDonRight,                           //
     {Menu::Descriptor::Type::Value,                                           //
      "Trg Level Right Don",                                                   //
      {{"", Menu::Descriptor::Action::SetDrumDoubleTriggerThresholdDonRight}}, //
      4095}},

    {Menu::Page::DrumDoubleTriggerThresholdKaRight,                           //
     {Menu::Descriptor::Type::Value,                                          //
      "Trg Level Right Ka",                                                   //
      {{"", Menu::Descriptor::Action::SetDrumDoubleTriggerThresholdKaRight}}, //
      4095}},

    {Menu::Page::DrumCutoffThresholdKaLeft,                           //
     {Menu::Descriptor::Type::Value,                                  //
      "Cutoff Threshold Ka Left",                                     //
      {{"", Menu::Descriptor::Action::SetDrumCutoffThresholdKaLeft}}, //
      4095}},

    {Menu::Page::DrumCutoffThresholdDonLeft,                           //
     {Menu::Descriptor::Type::Value,                                   //
      "Cutoff Threshold Don Left",                                     //
      {{"", Menu::Descriptor::Action::SetDrumCutoffThresholdDonLeft}}, //
      4095}},

    {Menu::Page::DrumCutoffThresholdDonRight,                           //
     {Menu::Descriptor::Type::Value,                                    //
      "Cutoff Threshold Don Right",                                     //
      {{"", Menu::Descriptor::Action::SetDrumCutoffThresholdDonRight}}, //
      4095}},

    {Menu::Page::DrumCutoffThresholdKaRight,                           //
     {Menu::Descriptor::Type::Value,                                   //
      "Cutoff Threshold Ka Right",                                     //
      {{"", Menu::Descriptor::Action::SetDrumCutoffThresholdKaRight}}, //
      4095}},

    {Menu::Page::Led,                                                           //
     {Menu::Descriptor::Type::Menu,                                             //
      "LED Settings",                                                           //
      {{"Brightness", Menu::Descriptor::Action::GotoPageLedBrightness},         //
       {"Plyr Color", Menu::Descriptor::Action::GotoPageLedEnablePlayerColor}}, //
      0}},                                                                      //

    {Menu::Page::LedBrightness,                           //
     {Menu::Descriptor::Type::Value,                      //
      "LED Brightness",                                   //
      {{"", Menu::Descriptor::Action::SetLedBrightness}}, //
      UINT8_MAX}},                                        //

    {Menu::Page::LedEnablePlayerColor,                           //
     {Menu::Descriptor::Type::Toggle,                            //
      "Player Color (PS4)",                                      //
      {{"", Menu::Descriptor::Action::SetLedEnablePlayerColor}}, //
      0}},                                                       //

    {Menu::Page::Reset,                              //
     {Menu::Descriptor::Type::Menu,                  //
      "Reset all Settings?",                         //
      {{"No", Menu::Descriptor::Action::GotoParent}, //
       {"Yes", Menu::Descriptor::Action::DoReset}},  //
      0}},                                           //

    {Menu::Page::ClearMacro,                              //
     {Menu::Descriptor::Type::Menu,                       //
      "Clear Macro?",                                     //
      {{"No", Menu::Descriptor::Action::GotoParent},      //
       {"Yes", Menu::Descriptor::Action::DoClearMacro}},  //
      0}},                                                //

    {Menu::Page::Bootsel,                                         //
     {Menu::Descriptor::Type::Menu,                               //
      "Reboot to Flash Mode",                                     //
      {{"Reboot?", Menu::Descriptor::Action::DoRebootToBootsel}}, //
      0}},                                                        //

    {Menu::Page::BootselMsg,                         //
     {Menu::Descriptor::Type::RebootInfo,            //
      "Ready to Flash...",                           //
      {{"BOOTSEL", Menu::Descriptor::Action::None}}, //
      0}},                                           //
};
// NOLINTEND(modernize-use-designated-initializers)

Menu::Buttons::Buttons()
    : m_states({{Id::Up, {}}, {Id::Down, {}}, {Id::Left, {}}, {Id::Right, {}}, {Id::Confirm, {}}, {Id::Back, {}}}) {}

void Menu::Buttons::update(const InputState::Controller &controller_state) {
    static const uint32_t repeat_delay = 1000;
    static const uint32_t repeat_interval = 20;
    static const uint32_t fast_repeat_delay = 5000;
    static const uint32_t fast_repeat_interval = 2;

    auto handle_button = [](State &button_state, bool input_state) {
        if (input_state) {
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            switch (button_state.repeat) {
            case State::Repeat::Idle:
                button_state.pressed = true;
                button_state.repeat = State::Repeat::RepeatDelay;
                button_state.pressed_since = now;
                break;
            case State::Repeat::RepeatDelay:
                if ((now - button_state.pressed_since) > repeat_delay) {
                    button_state.pressed = true;
                    button_state.repeat = State::Repeat::Repeat;
                    button_state.last_repeat = now;
                } else {
                    button_state.pressed = false;
                }
                break;
            case State::Repeat::Repeat:
                if ((now - button_state.pressed_since) > fast_repeat_delay) {
                    button_state.pressed = true;
                    button_state.repeat = State::Repeat::FastRepeat;
                    button_state.last_repeat = now;
                } else if ((now - button_state.last_repeat) > repeat_interval) {
                    button_state.pressed = true;
                    button_state.last_repeat = now;
                } else {
                    button_state.pressed = false;
                }
                break;
            case State::Repeat::FastRepeat:
                if ((now - button_state.last_repeat) > fast_repeat_interval) {
                    button_state.pressed = true;
                    button_state.last_repeat = now;
                } else {
                    button_state.pressed = false;
                }
                break;
            }
        } else {
            button_state.pressed = false;
            button_state.repeat = State::Repeat::Idle;
        }
    };

    handle_button(m_states.at(Id::Up), controller_state.dpad.up);
    handle_button(m_states.at(Id::Down), controller_state.dpad.down);
    handle_button(m_states.at(Id::Left), controller_state.dpad.left);
    handle_button(m_states.at(Id::Right), controller_state.dpad.right);
    handle_button(m_states.at(Id::Confirm), controller_state.buttons.east);
    handle_button(m_states.at(Id::Back), controller_state.buttons.south);
}

bool Menu::Buttons::getPressed(Id id) const { return m_states.at(id).pressed; }

Menu::Menu(std::shared_ptr<SettingsStore> settings_store, std::function<void()> clear_macro)
    : m_store(std::move(settings_store)), m_clear_macro(std::move(clear_macro)) {};

void Menu::activate() {
    m_state_stack = std::stack<State>({{.page = Page::Main, .selected_value = 0, .original_value = 0}});
    m_active = true;
}

uint16_t Menu::getCurrentValue(Menu::Page page) {
    switch (page) {
    case Page::DeviceMode:
        return static_cast<uint16_t>(m_store->getUsbMode());
    case Page::DrumDebounceDelay:
        return m_store->getDebounceDelay();
    case Page::DrumKeyTimeout:
        return m_store->getKeyTimeoutMs();
    case Page::DrumDonDebounce:
        return m_store->getDonDebounceMs();
    case Page::DrumKatDebounce:
        return m_store->getKatDebounceMs();
    case Page::DrumCrosstalkDebounce:
        return m_store->getCrosstalkDebounceMs();
    case Page::DrumDoubleTrigger:
        return static_cast<uint16_t>(m_store->getDoubleTriggerMode());
    case Page::DrumTriggerThresholdKaLeft:
        return m_store->getTriggerThresholds().ka_left;
    case Page::DrumTriggerThresholdDonLeft:
        return m_store->getTriggerThresholds().don_left;
    case Page::DrumTriggerThresholdDonRight:
        return m_store->getTriggerThresholds().don_right;
    case Page::DrumTriggerThresholdKaRight:
        return m_store->getTriggerThresholds().ka_right;
    case Page::DrumDoubleTriggerThresholdKaLeft:
        return m_store->getDoubleTriggerThresholds().ka_left;
    case Page::DrumDoubleTriggerThresholdDonLeft:
        return m_store->getDoubleTriggerThresholds().don_left;
    case Page::DrumDoubleTriggerThresholdDonRight:
        return m_store->getDoubleTriggerThresholds().don_right;
    case Page::DrumDoubleTriggerThresholdKaRight:
        return m_store->getDoubleTriggerThresholds().ka_right;
    case Page::DrumCutoffThresholdKaLeft:
        return m_store->getCutoffThresholds().ka_left;
    case Page::DrumCutoffThresholdDonLeft:
        return m_store->getCutoffThresholds().don_left;
    case Page::DrumCutoffThresholdDonRight:
        return m_store->getCutoffThresholds().don_right;
    case Page::DrumCutoffThresholdKaRight:
        return m_store->getCutoffThresholds().ka_right;
    case Page::LedBrightness:
        return m_store->getLedBrightness();
    case Page::LedEnablePlayerColor:
        return static_cast<uint16_t>(m_store->getLedEnablePlayerColor());
    case Page::Main:
    case Page::Drum:
    case Page::DrumDebounce:
    case Page::DrumTriggerThresholds:
    case Page::DrumDoubleTriggerThresholds:
    case Page::DrumCutoffThresholds:
    case Page::Led:
    case Page::Reset:
    case Page::ClearMacro:
    case Page::Bootsel:
    case Page::BootselMsg:
    case Page::Version:
        break;
    case Page::PS4Auth:
        return m_store->hasPS4AuthCredentials() ? 1 : 0;
    case Page::MacAddress:
        break;
    }

    return 0;
}

void Menu::gotoPage(Menu::Page page) {
    const auto current_value = getCurrentValue(page);

    m_state_stack.push({page, current_value, current_value});
}

void Menu::gotoParent(bool do_restore) {
    const auto current_state = m_state_stack.top();

    if (current_state.page == Page::Main) {
        m_active = false;
    }

    if (do_restore) {
        switch (current_state.page) {
        case Page::DeviceMode:
            m_store->setUsbMode(static_cast<usb_mode_t>(current_state.original_value));
            break;
        case Page::DrumDebounceDelay:
            m_store->setDebounceDelay(current_state.original_value);
            break;
        case Page::DrumKeyTimeout:
            m_store->setKeyTimeoutMs(current_state.original_value);
            break;
        case Page::DrumDonDebounce:
            m_store->setDonDebounceMs(current_state.original_value);
            break;
        case Page::DrumKatDebounce:
            m_store->setKatDebounceMs(current_state.original_value);
            break;
        case Page::DrumCrosstalkDebounce:
            m_store->setCrosstalkDebounceMs(current_state.original_value);
            break;
        case Page::DrumTriggerThresholdKaLeft: {
            auto thresholds = m_store->getTriggerThresholds();

            thresholds.ka_left = current_state.original_value;
            m_store->setTriggerThresholds(thresholds);
        } break;
        case Page::DrumTriggerThresholdDonLeft: {
            auto thresholds = m_store->getTriggerThresholds();

            thresholds.don_left = current_state.original_value;
            m_store->setTriggerThresholds(thresholds);
        } break;
        case Page::DrumTriggerThresholdDonRight: {
            auto thresholds = m_store->getTriggerThresholds();

            thresholds.don_right = current_state.original_value;
            m_store->setTriggerThresholds(thresholds);
        } break;
        case Page::DrumTriggerThresholdKaRight: {
            auto thresholds = m_store->getTriggerThresholds();

            thresholds.ka_right = current_state.original_value;
            m_store->setTriggerThresholds(thresholds);
        } break;
        case Page::DrumDoubleTrigger:
            m_store->setDoubleTriggerMode(
                static_cast<Peripherals::Drum::Config::DoubleTriggerMode>(current_state.original_value));
            break;
        case Page::DrumDoubleTriggerThresholdKaLeft: {
            auto thresholds = m_store->getDoubleTriggerThresholds();

            thresholds.ka_left = current_state.original_value;
            m_store->setDoubleTriggerThresholds(thresholds);
        } break;
        case Page::DrumDoubleTriggerThresholdDonLeft: {
            auto thresholds = m_store->getDoubleTriggerThresholds();

            thresholds.don_left = current_state.original_value;
            m_store->setDoubleTriggerThresholds(thresholds);
        } break;
        case Page::DrumDoubleTriggerThresholdDonRight: {
            auto thresholds = m_store->getDoubleTriggerThresholds();

            thresholds.don_right = current_state.original_value;
            m_store->setDoubleTriggerThresholds(thresholds);
        } break;
        case Page::DrumDoubleTriggerThresholdKaRight: {
            auto thresholds = m_store->getDoubleTriggerThresholds();

            thresholds.ka_right = current_state.original_value;
            m_store->setDoubleTriggerThresholds(thresholds);
        } break;
        case Page::DrumCutoffThresholdKaLeft: {
            auto thresholds = m_store->getCutoffThresholds();

            thresholds.ka_left = current_state.original_value;
            m_store->setCutoffThresholds(thresholds);
        } break;
        case Page::DrumCutoffThresholdDonLeft: {
            auto thresholds = m_store->getCutoffThresholds();

            thresholds.don_left = current_state.original_value;
            m_store->setCutoffThresholds(thresholds);
        } break;
        case Page::DrumCutoffThresholdDonRight: {
            auto thresholds = m_store->getCutoffThresholds();

            thresholds.don_right = current_state.original_value;
            m_store->setCutoffThresholds(thresholds);
        } break;
        case Page::DrumCutoffThresholdKaRight: {
            auto thresholds = m_store->getCutoffThresholds();

            thresholds.ka_right = current_state.original_value;
            m_store->setCutoffThresholds(thresholds);
        } break;
        case Page::LedBrightness:
            m_store->setLedBrightness(current_state.original_value);
            break;
        case Page::LedEnablePlayerColor:
            m_store->setLedEnablePlayerColor(static_cast<bool>(current_state.original_value));
            break;
        case Page::Main:
        case Page::Drum:
        case Page::DrumDebounce:
        case Page::DrumTriggerThresholds:
        case Page::DrumDoubleTriggerThresholds:
        case Page::DrumCutoffThresholds:
        case Page::Led:
        case Page::Reset:
        case Page::ClearMacro:
        case Page::Bootsel:
        case Page::BootselMsg:
        case Page::Version:
        case Page::PS4Auth:
        case Page::MacAddress:
            break;
        }
    }

    m_state_stack.pop();
}

void Menu::performAction(Descriptor::Action action, uint16_t value) {
    switch (action) {
    case Descriptor::Action::None:
        break;
    case Descriptor::Action::GotoParent:
        gotoParent(false);
        break;
    case Descriptor::Action::GotoPageDeviceMode:
        gotoPage(Page::DeviceMode);
        break;
    case Descriptor::Action::GotoPageDrum:
        gotoPage(Page::Drum);
        break;
    case Descriptor::Action::GotoPageDrumDoubleTrigger:
        gotoPage(Page::DrumDoubleTrigger);
        break;
    case Descriptor::Action::GotoPageDrumTriggerThresholds:
        gotoPage(Page::DrumTriggerThresholds);
        break;
    case Descriptor::Action::GotoPageDrumCutoffThresholds:
        gotoPage(Page::DrumCutoffThresholds);
        break;
    case Descriptor::Action::GotoPageDrumDoubleTriggerThresholds:
        m_store->setDoubleTriggerMode(Peripherals::Drum::Config::DoubleTriggerMode::Threshold);
        gotoPage(Page::DrumDoubleTriggerThresholds);
        break;
    case Descriptor::Action::GotoPageLed:
        gotoPage(Page::Led);
        break;
    case Descriptor::Action::GotoPageReset:
        gotoPage(Page::Reset);
        break;
    case Descriptor::Action::GotoPageClearMacro:
        gotoPage(Page::ClearMacro);
        break;
    case Descriptor::Action::GotoPageBootsel:
        gotoPage(Page::Bootsel);
        break;
    case Descriptor::Action::GotoPageVersion:
        gotoPage(Page::Version);
        break;
    case Descriptor::Action::GotoPagePS4Auth:
        gotoPage(Page::PS4Auth);
        break;
    case Descriptor::Action::GotoPageMacAddress:
        gotoPage(Page::MacAddress);
        break;
    case Descriptor::Action::GotoPageDrumDebounceDelay:
        gotoPage(Page::DrumDebounceDelay);
        break;
    case Descriptor::Action::GotoPageDrumKeyTimeout:
        gotoPage(Page::DrumKeyTimeout);
        break;
    case Descriptor::Action::GotoPageDrumDebounce:
        gotoPage(Page::DrumDebounce);
        break;
    case Descriptor::Action::GotoPageDrumDonDebounce:
        gotoPage(Page::DrumDonDebounce);
        break;
    case Descriptor::Action::GotoPageDrumKatDebounce:
        gotoPage(Page::DrumKatDebounce);
        break;
    case Descriptor::Action::GotoPageDrumCrosstalkDebounce:
        gotoPage(Page::DrumCrosstalkDebounce);
        break;
    case Descriptor::Action::GotoPageDrumTriggerThresholdKaLeft:
        gotoPage(Page::DrumTriggerThresholdKaLeft);
        break;
    case Descriptor::Action::GotoPageDrumTriggerThresholdDonLeft:
        gotoPage(Page::DrumTriggerThresholdDonLeft);
        break;
    case Descriptor::Action::GotoPageDrumTriggerThresholdDonRight:
        gotoPage(Page::DrumTriggerThresholdDonRight);
        break;
    case Descriptor::Action::GotoPageDrumTriggerThresholdKaRight:
        gotoPage(Page::DrumTriggerThresholdKaRight);
        break;
    case Descriptor::Action::GotoPageDrumDoubleTriggerThresholdKaLeft:
        gotoPage(Page::DrumDoubleTriggerThresholdKaLeft);
        break;
    case Descriptor::Action::GotoPageDrumDoubleTriggerThresholdDonLeft:
        gotoPage(Page::DrumDoubleTriggerThresholdDonLeft);
        break;
    case Descriptor::Action::GotoPageDrumDoubleTriggerThresholdDonRight:
        gotoPage(Page::DrumDoubleTriggerThresholdDonRight);
        break;
    case Descriptor::Action::GotoPageDrumDoubleTriggerThresholdKaRight:
        gotoPage(Page::DrumDoubleTriggerThresholdKaRight);
        break;
    case Descriptor::Action::GotoPageDrumCutoffThresholdKaLeft:
        gotoPage(Page::DrumCutoffThresholdKaLeft);
        break;
    case Descriptor::Action::GotoPageDrumCutoffThresholdDonLeft:
        gotoPage(Page::DrumCutoffThresholdDonLeft);
        break;
    case Descriptor::Action::GotoPageDrumCutoffThresholdDonRight:
        gotoPage(Page::DrumCutoffThresholdDonRight);
        break;
    case Descriptor::Action::GotoPageDrumCutoffThresholdKaRight:
        gotoPage(Page::DrumCutoffThresholdKaRight);
        break;
    case Descriptor::Action::GotoPageLedBrightness:
        gotoPage(Page::LedBrightness);
        break;
    case Descriptor::Action::GotoPageLedEnablePlayerColor:
        gotoPage(Page::LedEnablePlayerColor);
        break;
    case Descriptor::Action::SetUsbMode:
        m_store->setUsbMode(static_cast<usb_mode_t>(value));
        break;
    case Descriptor::Action::SetDrumDebounceDelay:
        m_store->setDebounceDelay(value);
        break;
    case Descriptor::Action::SetDrumKeyTimeout:
        m_store->setKeyTimeoutMs(value);
        break;
    case Descriptor::Action::SetDrumDonDebounce:
        m_store->setDonDebounceMs(value);
        break;
    case Descriptor::Action::SetDrumKatDebounce:
        m_store->setKatDebounceMs(value);
        break;
    case Descriptor::Action::SetDrumCrosstalkDebounce:
        m_store->setCrosstalkDebounceMs(value);
        break;
    case Descriptor::Action::SetDoubleTriggerOff:
        m_store->setDoubleTriggerMode(Peripherals::Drum::Config::DoubleTriggerMode::Off);
        gotoParent(false);
        break;
    case Descriptor::Action::SetDrumTriggerThresholdKaLeft: {
        auto thresholds = m_store->getTriggerThresholds();

        thresholds.ka_left = value;
        m_store->setTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumTriggerThresholdDonLeft: {
        auto thresholds = m_store->getTriggerThresholds();

        thresholds.don_left = value;
        m_store->setTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumTriggerThresholdDonRight: {
        auto thresholds = m_store->getTriggerThresholds();

        thresholds.don_right = value;
        m_store->setTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumTriggerThresholdKaRight: {
        auto thresholds = m_store->getTriggerThresholds();

        thresholds.ka_right = value;
        m_store->setTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumDoubleTriggerThresholdKaLeft: {
        auto thresholds = m_store->getDoubleTriggerThresholds();

        thresholds.ka_left = value;
        m_store->setDoubleTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumDoubleTriggerThresholdDonLeft: {
        auto thresholds = m_store->getDoubleTriggerThresholds();

        thresholds.don_left = value;
        m_store->setDoubleTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumDoubleTriggerThresholdDonRight: {
        auto thresholds = m_store->getDoubleTriggerThresholds();

        thresholds.don_right = value;
        m_store->setDoubleTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumDoubleTriggerThresholdKaRight: {
        auto thresholds = m_store->getDoubleTriggerThresholds();

        thresholds.ka_right = value;
        m_store->setDoubleTriggerThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumCutoffThresholdKaLeft: {
        auto thresholds = m_store->getCutoffThresholds();

        thresholds.ka_left = value;
        m_store->setCutoffThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumCutoffThresholdDonLeft: {
        auto thresholds = m_store->getCutoffThresholds();

        thresholds.don_left = value;
        m_store->setCutoffThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumCutoffThresholdDonRight: {
        auto thresholds = m_store->getCutoffThresholds();

        thresholds.don_right = value;
        m_store->setCutoffThresholds(thresholds);
    } break;
    case Descriptor::Action::SetDrumCutoffThresholdKaRight: {
        auto thresholds = m_store->getCutoffThresholds();

        thresholds.ka_right = value;
        m_store->setCutoffThresholds(thresholds);
    } break;
    case Descriptor::Action::SetLedBrightness:
        m_store->setLedBrightness(value);
        break;
    case Descriptor::Action::SetLedEnablePlayerColor:
        m_store->setLedEnablePlayerColor(static_cast<bool>(value));
        break;
    case Descriptor::Action::DoReset:
        m_store->reset();
        break;
    case Descriptor::Action::DoClearMacro:
        if (m_clear_macro) {
            m_clear_macro();
        }
        gotoParent(false);
        break;
    case Descriptor::Action::DoRebootToBootsel:
        m_store->scheduleReboot(true);
        gotoPage(Page::BootselMsg);
        break;
    }
}

void Menu::update(const InputState::Controller &controller_state) {
    m_buttons.update(controller_state);

    State &current_state = m_state_stack.top();

    auto descriptor_it = descriptors.find(current_state.page);
    if (descriptor_it == descriptors.end()) {
        assert(false);
        return;
    }

    if (descriptor_it->second.type == Descriptor::Type::RebootInfo) {
        m_active = false;
    } else if (m_buttons.getPressed(Buttons::Id::Left)) {
        switch (descriptor_it->second.type) {
        case Descriptor::Type::Toggle:
            current_state.selected_value = current_state.selected_value == 0 ? 1 : 0;
            performAction(descriptor_it->second.items.at(0).second, current_state.selected_value);
            break;
        case Descriptor::Type::Selection:
            if (current_state.selected_value == 0) {
                current_state.selected_value = descriptor_it->second.items.size() - 1;
            } else {
                current_state.selected_value--;
            }
            performAction(descriptor_it->second.items.at(current_state.selected_value).second,
                          current_state.selected_value);
            break;
        case Descriptor::Type::Menu:
            if (current_state.selected_value == 0) {
                current_state.selected_value = descriptor_it->second.items.size() - 1;
            } else {
                current_state.selected_value--;
            }
            break;
        case Descriptor::Type::Value:
        case Descriptor::Type::RebootInfo:
        case Descriptor::Type::Info:
            break;
        }
    } else if (m_buttons.getPressed(Buttons::Id::Right)) {
        switch (descriptor_it->second.type) {
        case Descriptor::Type::Toggle:
            current_state.selected_value = current_state.selected_value == 0 ? 1 : 0;
            performAction(descriptor_it->second.items.at(0).second, current_state.selected_value);
            break;
        case Descriptor::Type::Selection:
            if (current_state.selected_value == descriptor_it->second.items.size() - 1) {
                current_state.selected_value = 0;
            } else {
                current_state.selected_value++;
            }
            performAction(descriptor_it->second.items.at(current_state.selected_value).second,
                          current_state.selected_value);
            break;
        case Descriptor::Type::Menu:
            if (current_state.selected_value == descriptor_it->second.items.size() - 1) {
                current_state.selected_value = 0;
            } else {
                current_state.selected_value++;
            }
            break;
        case Descriptor::Type::Value:
        case Descriptor::Type::RebootInfo:
        case Descriptor::Type::Info:
            break;
        }
    } else if (m_buttons.getPressed(Buttons::Id::Up)) {
        switch (descriptor_it->second.type) {
        case Descriptor::Type::Value:
            if (current_state.selected_value < descriptor_it->second.max_value) {
                current_state.selected_value++;
                performAction(descriptor_it->second.items.at(0).second, current_state.selected_value);
            }
            break;
        case Descriptor::Type::Toggle:
        case Descriptor::Type::Selection:
        case Descriptor::Type::Menu:
        case Descriptor::Type::RebootInfo:
        case Descriptor::Type::Info:
            break;
        }
    } else if (m_buttons.getPressed(Buttons::Id::Down)) {
        switch (descriptor_it->second.type) {
        case Descriptor::Type::Value:
            if (current_state.selected_value > 0) {
                current_state.selected_value--;
                performAction(descriptor_it->second.items.at(0).second, current_state.selected_value);
            }
            break;
        case Descriptor::Type::Toggle:
        case Descriptor::Type::Selection:
        case Descriptor::Type::Menu:
        case Descriptor::Type::RebootInfo:
        case Descriptor::Type::Info:
            break;
        }
    } else if (m_buttons.getPressed(Buttons::Id::Back)) {
        switch (descriptor_it->second.type) {
        case Descriptor::Type::Value:
        case Descriptor::Type::Toggle:
        case Descriptor::Type::Selection:
            gotoParent(true);
            break;
        case Descriptor::Type::Menu:
        case Descriptor::Type::Info:
            gotoParent(false);
            break;
        case Descriptor::Type::RebootInfo:
            break;
        }
    } else if (m_buttons.getPressed(Buttons::Id::Confirm)) {
        switch (descriptor_it->second.type) {
        case Descriptor::Type::Value:
        case Descriptor::Type::Toggle:
        case Descriptor::Type::Selection:
            gotoParent(false);
            break;
        case Descriptor::Type::Menu:
            performAction(descriptor_it->second.items.at(current_state.selected_value).second,
                          current_state.selected_value);
            break;
        case Descriptor::Type::RebootInfo:
        case Descriptor::Type::Info:
            break;
        }
    }
}

bool Menu::active() const { return m_active; }

Menu::State Menu::getState() const { return m_state_stack.top(); }

} // namespace Doncon::Utils
