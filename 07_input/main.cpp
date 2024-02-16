#include <ac/ac.h>

#define RIF(x)                                                                 \
  do                                                                           \
  {                                                                            \
    ac_result res = (x);                                                       \
    if (res != ac_result_success)                                              \
    {                                                                          \
      return;                                                                  \
    }                                                                          \
  }                                                                            \
  while (false)

static inline const char*
ac_mouse_button_to_string(ac_mouse_button button)
{
  switch (button)
  {
  case ac_mouse_button_left:
    return "ac_mouse_button_left";
  case ac_mouse_button_middle:
    return "ac_mouse_button_middle";
  case ac_mouse_button_right:
    return "ac_mouse_button_right";
  case ac_mouse_button_forward:
    return "ac_mouse_button_forward";
  case ac_mouse_button_back:
    return "ac_mouse_button_back";
  default:
    break;
  }

  return "ac_mouse_button_unknown";
}

static inline const char*
ac_key_to_string(ac_key key)
{
  switch (key)
  {
  case ac_key_a:
    return "ac_key_a";
  case ac_key_b:
    return "ac_key_b";
  case ac_key_c:
    return "ac_key_c";
  case ac_key_d:
    return "ac_key_d";
  case ac_key_e:
    return "ac_key_e";
  case ac_key_f:
    return "ac_key_f";
  case ac_key_g:
    return "ac_key_g";
  case ac_key_h:
    return "ac_key_h";
  case ac_key_i:
    return "ac_key_i";
  case ac_key_j:
    return "ac_key_j";
  case ac_key_k:
    return "ac_key_k";
  case ac_key_l:
    return "ac_key_l";
  case ac_key_m:
    return "ac_key_m";
  case ac_key_n:
    return "ac_key_n";
  case ac_key_o:
    return "ac_key_o";
  case ac_key_p:
    return "ac_key_p";
  case ac_key_q:
    return "ac_key_q";
  case ac_key_r:
    return "ac_key_r";
  case ac_key_s:
    return "ac_key_s";
  case ac_key_t:
    return "ac_key_t";
  case ac_key_u:
    return "ac_key_u";
  case ac_key_v:
    return "ac_key_v";
  case ac_key_w:
    return "ac_key_w";
  case ac_key_x:
    return "ac_key_x";
  case ac_key_y:
    return "ac_key_y";
  case ac_key_z:
    return "ac_key_z";
  case ac_key_zero:
    return "ac_key_zero";
  case ac_key_one:
    return "ac_key_one";
  case ac_key_two:
    return "ac_key_two";
  case ac_key_three:
    return "ac_key_three";
  case ac_key_four:
    return "ac_key_four";
  case ac_key_five:
    return "ac_key_five";
  case ac_key_six:
    return "ac_key_six";
  case ac_key_seven:
    return "ac_key_seven";
  case ac_key_eight:
    return "ac_key_eight";
  case ac_key_nine:
    return "ac_key_nine";
  case ac_key_return:
    return "ac_key_return";
  case ac_key_escape:
    return "ac_key_escape";
  case ac_key_backspace:
    return "ac_key_backspace";
  case ac_key_tab:
    return "ac_key_tab";
  case ac_key_spacebar:
    return "ac_key_spacebar";
  case ac_key_hyphen:
    return "ac_key_hyphen";
  case ac_key_equal_sign:
    return "ac_key_equal_sign";
  case ac_key_open_bracket:
    return "ac_key_open_bracket";
  case ac_key_close_bracket:
    return "ac_key_close_bracket";
  case ac_key_backslash:
    return "ac_key_backslash";
  case ac_key_semicolon:
    return "ac_key_semicolon";
  case ac_key_quote:
    return "ac_key_quote";
  case ac_key_tilde:
    return "ac_key_tilde";
  case ac_key_comma:
    return "ac_key_comma";
  case ac_key_period:
    return "ac_key_period";
  case ac_key_slash:
    return "ac_key_slash";
  case ac_key_caps_lock:
    return "ac_key_caps_lock";
  case ac_key_f1:
    return "ac_key_f1";
  case ac_key_f2:
    return "ac_key_f2";
  case ac_key_f3:
    return "ac_key_f3";
  case ac_key_f4:
    return "ac_key_f4";
  case ac_key_f5:
    return "ac_key_f5";
  case ac_key_f6:
    return "ac_key_f6";
  case ac_key_f7:
    return "ac_key_f7";
  case ac_key_f8:
    return "ac_key_f8";
  case ac_key_f9:
    return "ac_key_f9";
  case ac_key_f10:
    return "ac_key_f10";
  case ac_key_f11:
    return "ac_key_f11";
  case ac_key_f12:
    return "ac_key_f12";
  case ac_key_print_screen:
    return "ac_key_print_screen";
  case ac_key_scroll_lock:
    return "ac_key_scroll_lock";
  case ac_key_pause:
    return "ac_key_pause";
  case ac_key_insert:
    return "ac_key_insert";
  case ac_key_home:
    return "ac_key_home";
  case ac_key_page_up:
    return "ac_key_page_up";
  case ac_key_delete:
    return "ac_key_delete";
  case ac_key_end:
    return "ac_key_end";
  case ac_key_page_down:
    return "ac_key_page_down";
  case ac_key_right_arrow:
    return "ac_key_right_arrow";
  case ac_key_left_arrow:
    return "ac_key_left_arrow";
  case ac_key_down_arrow:
    return "ac_key_down_arrow";
  case ac_key_up_arrow:
    return "ac_key_up_arrow";
  case ac_key_keypad_num_lock:
    return "ac_key_keypad_num_lock";
  case ac_key_keypad_slash:
    return "ac_key_keypad_slash";
  case ac_key_keypad_asterisk:
    return "ac_key_keypad_asterisk";
  case ac_key_keypad_hyphen:
    return "ac_key_keypad_hyphen";
  case ac_key_keypad_plus:
    return "ac_key_keypad_plus";
  case ac_key_keypad_enter:
    return "ac_key_keypad_enter";
  case ac_key_keypad_0:
    return "ac_key_keypad_0";
  case ac_key_keypad_1:
    return "ac_key_keypad_1";
  case ac_key_keypad_2:
    return "ac_key_keypad_2";
  case ac_key_keypad_3:
    return "ac_key_keypad_3";
  case ac_key_keypad_4:
    return "ac_key_keypad_4";
  case ac_key_keypad_5:
    return "ac_key_keypad_5";
  case ac_key_keypad_6:
    return "ac_key_keypad_6";
  case ac_key_keypad_7:
    return "ac_key_keypad_7";
  case ac_key_keypad_8:
    return "ac_key_keypad_8";
  case ac_key_keypad_9:
    return "ac_key_keypad_9";
  case ac_key_left_control:
    return "ac_key_left_control";
  case ac_key_left_shift:
    return "ac_key_left_shift";
  case ac_key_left_alt:
    return "ac_key_left_alt";
  case ac_key_left_super:
    return "ac_key_left_super";
  case ac_key_right_control:
    return "ac_key_right_control";
  case ac_key_right_shift:
    return "ac_key_right_shift";
  case ac_key_right_alt:
    return "ac_key_right_alt";
  case ac_key_right_super:
    return "ac_key_right_super";
  default:
    break;
  }

  return "ac_key_unknown";
}

static inline const char*
ac_gamepad_button_to_string(ac_gamepad_button button)
{
  switch (button)
  {
  case ac_gamepad_button_a:
    return "ac_gamepad_button_a";
  case ac_gamepad_button_b:
    return "ac_gamepad_button_b";
  case ac_gamepad_button_x:
    return "ac_gamepad_button_x";
  case ac_gamepad_button_y:
    return "ac_gamepad_button_y";
  case ac_gamepad_button_left_shoulder:
    return "ac_gamepad_button_left_shoulder";
  case ac_gamepad_button_right_shoulder:
    return "ac_gamepad_button_right_shoulder";
  case ac_gamepad_button_left_thumbstick:
    return "ac_gamepad_button_left_thumbstick";
  case ac_gamepad_button_right_thumbstick:
    return "ac_gamepad_button_right_thumbstick";
  case ac_gamepad_button_dpad_left:
    return "ac_gamepad_button_dpad_left";
  case ac_gamepad_button_dpad_right:
    return "ac_gamepad_button_dpad_right";
  case ac_gamepad_button_dpad_up:
    return "ac_gamepad_button_dpad_up";
  case ac_gamepad_button_dpad_down:
    return "ac_gamepad_button_dpad_down";
  case ac_gamepad_button_menu:
    return "ac_gamepad_button_menu";
  case ac_gamepad_button_options:
    return "ac_gamepad_button_options";
  default:
    break;
  }

  return "ac_gamepad_button_unknown";
}

static inline const char*
ac_gamepad_axis_to_string(ac_gamepad_axis axis)
{
  switch (axis)
  {
  case ac_gamepad_axis_left_trigger:
    return "ac_gamepad_axis_left_trigger";
  case ac_gamepad_axis_left_thumbstick_x:
    return "ac_gamepad_axis_left_thumbstick_x";
  case ac_gamepad_axis_left_thumbstick_y:
    return "ac_gamepad_axis_left_thumbstick_y";
  case ac_gamepad_axis_right_trigger:
    return "ac_gamepad_axis_right_trigger";
  case ac_gamepad_axis_right_thumbstick_x:
    return "ac_gamepad_axis_right_thumbstick_x";
  case ac_gamepad_axis_right_thumbstick_y:
    return "ac_gamepad_axis_right_thumbstick_y";
  default:
    break;
  }

  return "ac_gamepad_axis_unknown";
}

class App {
private:
  static constexpr const char* APP_NAME = "07_input";

  bool             m_running = {};
  ac_gamepad_state m_gamepad_states[AC_MAX_GAMEPADS] = {};

  static void
  window_callback(const ac_window_event* event, void* ud);
  static void
  input_callback(const ac_input_event* event, void* ud);

public:
  App();

  ~App();

  ac_result
  run();
};

App::App()
{
  {
    ac_init_info info = {};
    info.app_name = App::APP_NAME;
    info.enable_memory_manager = AC_INCLUDE_DEBUG;
    RIF(ac_init(&info));
  }
  RIF(ac_init_window(App::APP_NAME));

  {
    ac_input_info info = {};
    info.callback = App::input_callback;
    info.callback_data = this;
    RIF(ac_init_input(&info));
  }

  {
    ac_window_state state = {};
    state.callback = App::window_callback;
    state.callback_data = this;
    state.width = 640;
    state.height = 480;
    RIF(ac_window_set_state(&state));
  }

  m_running = true;
}

App::~App()
{
  ac_shutdown_input();
  ac_shutdown_window();
  ac_shutdown();
}

void
App::window_callback(const ac_window_event* event, void* ud)
{
  App* p = static_cast<App*>(ud);

  switch (event->type)
  {
  case ac_window_event_type_close:
  {
    p->m_running = false;
    break;
  }
  default:
  {
    break;
  }
  }
}

void
App::input_callback(const ac_input_event* event, void* ud)
{
  App* p = static_cast<App*>(ud);

  switch (event->type)
  {
  case ac_input_event_type_key_down:
  {
    AC_INFO(
      "ac_event_type_key_down: \n\tkey: %s",
      ac_key_to_string(event->key));
    break;
  }
  case ac_input_event_type_key_up:
  {
    AC_INFO("ac_event_type_key_up: \n\tkey: %s", ac_key_to_string(event->key));
    break;
  }
  case ac_input_event_type_mouse_button_down:
  {
    AC_INFO(
      "ac_event_type_mouse_button_down: \n\tbutton: %s",
      ac_mouse_button_to_string(event->mouse_button));
    break;
  }
  case ac_input_event_type_mouse_button_up:
  {
    AC_INFO(
      "ac_event_type_mouse_button_up: \n\tbutton: %s",
      ac_mouse_button_to_string(event->mouse_button));
    break;
  }
  case ac_input_event_type_mouse_move:
  {
    AC_INFO(
      "ac_event_type_mouse_move: \n\tdx: %f dy: %f",
      event->mouse_move.dx,
      event->mouse_move.dy);
    break;
  }
  default:
  {
    break;
  }
  }
}

ac_result
App::run()
{
  if (!m_running)
  {
    return ac_result_unknown_error;
  }

  while (m_running)
  {
    ac_window_poll_events();

    for (uint32_t i = 0; i < AC_MAX_GAMEPADS; ++i)
    {
      ac_gamepad_state* prev = &m_gamepad_states[i];
      ac_gamepad_state  curr;
      ac_input_get_gamepad_state(i, &curr);

      if (curr.connected != prev->connected)
      {
        if (curr.connected)
        {
          AC_INFO("gamepad connected %d", i);
        }
        else
        {
          AC_INFO("gamepad disconnected %d", i);
        }
      }

      if (!curr.connected)
      {
        *prev = curr;
        continue;
      }

      for (uint32_t b = 0; b < ac_gamepad_button_count; ++b)
      {
        if (prev->buttons[b] != curr.buttons[b])
        {
          if (curr.buttons[b])
          {
            AC_INFO(
              "gamepad button down: \n\tbutton: %s",
              ac_gamepad_button_to_string(ac_gamepad_button(b)));
          }
          else
          {
            AC_INFO(
              "gamepad button up: \n\tbutton: %s",
              ac_gamepad_button_to_string(ac_gamepad_button(b)));
          }
        }
      }

      for (uint32_t a = 0; a < ac_gamepad_axis_count; ++a)
      {
        if (prev->axis[a] != curr.axis[a])
        {
          AC_INFO(
            "gamepad axis: \n\taxis: %s\n\tvalue: %f",
            ac_gamepad_axis_to_string(ac_gamepad_axis(a)),
            curr.axis[a]);
        }
      }
      *prev = curr;
    }
  }

  return ac_result_success;
}

extern "C" ac_result
ac_main(uint32_t argc, char** argv)
{
  App*      app = new App {};
  ac_result res = app->run();
  delete app;
  return res;
}
