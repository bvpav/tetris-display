#include <MD_MAX72xx.h>

constexpr int GAME_DPY_WIDTH=8, GAME_DPY_HEIGHT=32;
constexpr int PREVIEW_DPY_WIDTH=8, PREVIEW_DPY_HEIGHT=32;
constexpr int GAME_DPY_CS_PIN=10, PREVIEW_DPY_CS_PIN=8; // Digital
constexpr int KEYPAD_OUT_PIN=0; // Analog
constexpr int ENC_CLK_PIN=2, ENC_DT_PIN=3, ENC_SW_PIN=4; // Digital
constexpr int BLINK_TIME_MS = 500;

enum struct Key : byte
{
  None, Up, Down, Left, Right, Drop
};

#define APPROX_EQ(val, ref) ((ref)-50 <= (val) && (val) <= (ref)+50)

Key key = Key::None;

Key get_pressed_key(uint8_t pin)
{
  int val = analogRead(pin);
  Key old_key = key;
  if (APPROX_EQ(val, 144))
    key = Key::Up;
  else if (APPROX_EQ(val, 328))
    key = Key::Down;
  else if (APPROX_EQ(val, 0))
    key = Key::Left;
  else if (APPROX_EQ(val, 504))
    key = Key::Right;
  else if (APPROX_EQ(val, 738))
    key = Key::Drop;
  else
    key = Key::None;
  return key != old_key ? key : Key::None;
}

struct Encoder
{
  uint8_t clk_pin, dt_pin, sw_pin;
  int rotation=0, last_clk=0;
  bool is_clockwise=false, button=false, has_rotated=false, was_button_pressed=false;

  Encoder(uint8_t clk_pin, uint8_t dt_pin, uint8_t sw_pin)
    : clk_pin(clk_pin), dt_pin(dt_pin), sw_pin(sw_pin)
  {
  }

  void begin()
  {
    pinMode(clk_pin, INPUT);
    pinMode(dt_pin, INPUT);
    pinMode(sw_pin, INPUT_PULLUP);
    last_clk = digitalRead(clk_pin);
  }

  void read()
  {
    int current_clk = digitalRead(clk_pin);
    if (!last_clk && current_clk)
    {
      if (digitalRead(dt_pin))
        ++rotation, is_clockwise=true;
      else
        --rotation, is_clockwise=false;
      has_rotated = true;
    }
    else
      has_rotated = false;
    last_clk = current_clk;

    bool new_button = !digitalRead(sw_pin);
    was_button_pressed = !button && new_button;
    button = new_button;
  }

  unsigned wrapped_rotation(unsigned max)
  {
    int rem = rotation % max;
    return 0 <= rem ? rem : rem + abs(max);
  }
};

MD_MAX72XX game_dpy(MD_MAX72XX::FC16_HW, GAME_DPY_CS_PIN, GAME_DPY_HEIGHT/8);
MD_MAX72XX preview_dpy(MD_MAX72XX::FC16_HW, PREVIEW_DPY_CS_PIN, PREVIEW_DPY_HEIGHT/8);
Encoder enc(ENC_CLK_PIN, ENC_DT_PIN, ENC_SW_PIN);

constexpr char *tetrominoes[] =
{
  "..X."
  "..X."
  "..X."
  "..X.",

  "..X."
  ".XX."
  "..X."
  "....",

  "...."
  ".XX."
  ".XX."
  "....",

  "..X."
  ".XX."
  ".X.."
  "....",

  ".X.."
  ".XX."
  "..X."
  "....",

  ".X.."
  ".X.."
  ".XX."
  "....",

  "..X."
  "..X."
  ".XX."
  "....",
};
constexpr size_t num_tetrominoes = sizeof tetrominoes / sizeof *tetrominoes;

const char (*rotations[])(int dx, int dy, int tetromino_idx) =
{
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][dy * 4 + dx]; },
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][12 + dy - (dx * 4)]; },
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][15 - (dy * 4) - dx]; },
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][3 - dy + (dx * 4)]; },
};
constexpr size_t num_rotations = sizeof rotations / sizeof *rotations;

int prev_tetromino_type = -1;

bool stack[GAME_DPY_WIDTH][GAME_DPY_HEIGHT]{false};

struct Tetromino
{
  int x = GAME_DPY_WIDTH/2 - 2, y = GAME_DPY_WIDTH/2 - 2, type=0, rotation=0;

  Tetromino()
  {}

  Tetromino(int x, int y, int type, int rotation)
    : x(x), y(y), type(type), rotation(rotation)
  {}

  bool get_pos(int dx, int dy)
  {
    return rotations[rotation](dx, dy, type) == 'X';
  }

  bool can_exist()
  {
    for (int dx=0; dx<4; ++dx)
      for (int dy=0; dy<4; ++dy)
        if (get_pos(dx, dy) && !is_pos_free(x+dx, y+dy))
          return false;
    return true;
  }

  void rotate()
  {
    int new_piece_rot = (rotation + 1) % num_rotations;
    if (Tetromino(x, y, type, new_piece_rot).can_exist())
    {
      rotation = new_piece_rot;
      // XXX: maybe don't redraw here, but outside??
      game_redraw();
    }
  }

  bool apply_gravity()
  {
    if (Tetromino(x, y+1, type, rotation).can_exist())
    {
      ++y;
      game_redraw();
    }
    else
      return place();
    return false;
  }

  bool apply_gravity_timed()
  {
    static unsigned long last_update = millis();
    unsigned long now = millis();
    if (now - last_update < 1000)
      return false;
    last_update = now;
    return apply_gravity();
  }

  void move_left()
  {
    if (Tetromino(x-1, y, type, rotation).can_exist())
    {
      --x;
      game_redraw();
    }
  }

  void move_right()
  {
    if (Tetromino(x+1, y, type, rotation).can_exist())
    {
      ++x;
      game_redraw();
    }
  }

  void drop()
  {
    while (Tetromino(x, y+1, type, rotation).can_exist())
      ++y;
  }

  bool place();
} tetromino, next_tetromino;

bool Tetromino::place()
{
  for (int dx=0; dx<4; ++dx)
      for (int dy=0; dy<4; ++dy)
        if (get_pos(dx, dy))
          stack[x+dx][y+dy] = true;
  for (int y=GAME_DPY_HEIGHT-1, offset=0; 0 <= y; --y)
  {
    if (stack[0][y] && stack[0][y] == stack[1][y] && stack[1][y] == stack[2][y] && stack[2][y] == stack[3][y] &&
          stack[3][y] == stack[4][y] && stack[4][y] == stack[5][y] && stack[5][y] == stack[6][y] && stack[6][y] == stack[7][y])
      ++offset;
    else
      for (int x=0; x<GAME_DPY_WIDTH; ++x)
        stack[x][y+offset] = stack[x][y];
  }
  if (next_tetromino.type == type || (0 <= prev_tetromino_type && next_tetromino.type == prev_tetromino_type))
    return true;
  prev_tetromino_type = type;
  *this = next_tetromino;
  y = 0;
  return false;
}

void draw_tetromino(MD_MAX72XX &dpy, const struct Tetromino &t)
{
  for (int dx=0; dx<4; ++dx)
    for (int dy=0; dy<4; ++dy)
      dpy.setPoint(GAME_DPY_WIDTH-t.x-1-dx, GAME_DPY_HEIGHT-t.y-1-dy, t.get_pos(dx, dy));
}

void draw_stack()
{
  for (int x=0; x<GAME_DPY_WIDTH; ++x)
    for (int y=0; y<GAME_DPY_HEIGHT; ++y)
      if (stack[x][y])
        game_dpy.setPoint(GAME_DPY_WIDTH-x-1, GAME_DPY_HEIGHT-y-1, true);
}

bool is_pos_free(int x, int y)
{
  return 0 <= x && x < GAME_DPY_WIDTH &&
    0 <= y && y < GAME_DPY_HEIGHT &&
    !stack[x][y];
}

void game_redraw()
{
  game_dpy.clear();
  draw_tetromino(game_dpy, tetromino);
  draw_stack();
}

void preview_redraw()
{
  preview_dpy.clear();
  draw_tetromino(preview_dpy, next_tetromino);
}

void preview_next_tetromino()
{
  next_tetromino.type = enc.wrapped_rotation(num_tetrominoes);
  preview_redraw();
}

void setup()
{
  Serial.begin(9600);
  game_dpy.begin();
  preview_dpy.begin();
  enc.begin();
}

void press_start_loop()
{
  game_dpy.clear();
  preview_redraw();
  while (true)
  {
    if (get_pressed_key(KEYPAD_OUT_PIN) != Key::None)
      return;
    enc.read();
    if (enc.has_rotated)
    {
      preview_next_tetromino();
      tetromino = next_tetromino;
    }
    delay(1);
  }
}

void blink_stack_loop()
{
  game_dpy.clear();
  unsigned long last_update = millis();
  bool is_on = false;
  tetromino = next_tetromino;
  while (true)
  {
    if (get_pressed_key(KEYPAD_OUT_PIN) != Key::None || (enc.read(), enc.was_button_pressed))
      return;
    unsigned long now = millis();
    if (BLINK_TIME_MS <= now - last_update)
    {
      last_update = now;
      is_on = !is_on;
      game_dpy.clear();
      if (is_on)
        draw_stack();
    }
  }
}

void blink_preview_loop()
{
  game_dpy.clear();
  draw_stack();
  preview_dpy.clear();
  unsigned long last_update = millis();
  bool is_on = false;
  tetromino = next_tetromino;
  while (true)
  {
    if (get_pressed_key(KEYPAD_OUT_PIN) != Key::None || (enc.read(), enc.was_button_pressed))
      return;
    unsigned long now = millis();
    if (BLINK_TIME_MS <= now - last_update)
    {
      last_update = now;
      is_on = !is_on;
      if (is_on)
        preview_redraw();
      else
        preview_dpy.clear();
    }
  }
}

void game_loop()
{
  bool should_drop = false;
  unsigned long drop_pressed_at;
  game_redraw();
  while (true)
  {
    switch (get_pressed_key(KEYPAD_OUT_PIN))
    {
      case Key::Up:
        tetromino.rotate();
        break;
      case Key::Down:
        if (tetromino.apply_gravity())
          return;
        break;
      case Key::Left:
        tetromino.move_left();
        break;
      case Key::Right:
        tetromino.move_right();
        break;
      case Key::Drop:
        should_drop = true;
        Serial.println("should drop");
        drop_pressed_at = millis();
        break;
      case Key::None:
        if (should_drop && 3000 <= millis() - drop_pressed_at)
        {
          tetromino.drop();
          if (tetromino.place())
            return;
          should_drop = false;
        }
        break;
    }
    enc.read();
    if (enc.has_rotated)
      preview_next_tetromino();
    if (enc.was_button_pressed)
      return;
    if (tetromino.apply_gravity_timed())
      return;
    delay(1);
  }
}

void loop()
{
  press_start_loop();
  game_loop();
  blink_stack_loop();
  blink_preview_loop();
  memset(stack, 0, sizeof stack);
}
