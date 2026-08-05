#include "input.h"
#include "test.h"

static void test_player_command_parsing(void) {
  const struct {
    char input;
    enum player_command expected;
  } cases[] = {
      {'k', PLAYER_COMMAND_MOVE_UP},
      {'j', PLAYER_COMMAND_MOVE_DOWN},
      {'h', PLAYER_COMMAND_MOVE_LEFT},
      {'l', PLAYER_COMMAND_MOVE_RIGHT},
      {'?', PLAYER_COMMAND_HELP},
      {'v', PLAYER_COMMAND_TOGGLE_DEBUG},
      {'c', PLAYER_COMMAND_GROW},
      {'q', PLAYER_COMMAND_QUIT},
      {'x', PLAYER_COMMAND_INVALID},
  };

  for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); index++)
    CHECK_EQ(input_parse_player_command(cases[index].input),
             cases[index].expected);
}

static void test_replay_command_parsing(void) {
  const struct {
    char input;
    enum replay_command expected;
  } cases[] = {
      {'r', REPLAY_COMMAND_RETRY},
      {'q', REPLAY_COMMAND_QUIT},
      {'x', REPLAY_COMMAND_INVALID},
  };

  for (size_t index = 0; index < sizeof(cases) / sizeof(*cases); index++)
    CHECK_EQ(input_parse_replay_command(cases[index].input),
             cases[index].expected);
}

int main(void) {
  test_player_command_parsing();
  test_replay_command_parsing();

  return test_finish("input");
}
