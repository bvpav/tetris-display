#include <MD_MAX72xx.h>

constexpr int DPY_WIDTH=8, DPY_HEIGHT=32;
constexpr int DPY_CS_PIN=10; // Digital
constexpr int KEYPAD_OUT_PIN=0; // Analog
constexpr int ENC_CLK_PIN=2, ENC_DT_PIN=3, ENC_SW_PIN=4; // Digital

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

MD_MAX72XX dpy(MD_MAX72XX::FC16_HW, DPY_CS_PIN, DPY_HEIGHT/8);
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

const char (*rotations[])(int dx, int dy, int tetromino_idx) = {
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][dy * 4 + dx]; },
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][12 + dy - (dx * 4)]; },
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][15 - (dy * 4) - dx]; },
  [tetrominoes](int dx, int dy, int tetromino_idx) { return tetrominoes[tetromino_idx][3 - dy + (dx * 4)]; },
};
constexpr size_t num_rotations = sizeof rotations / sizeof *rotations;


struct Tetromino
{
  int x=0, y=0, type=0, rotation=0;

  Tetromino()
  {}

  Tetromino(int x, int y, int type, int rotation)
    : x(x), y(y), type(type), rotation(rotation)
  {}

  bool get_pos(int dx, int dy)
  {
    return rotations[rotation](dx, dy, type) == 'X';
  }
} tetromino;

bool is_gravity_enabled=false;

bool stack[DPY_WIDTH][DPY_HEIGHT]{false};

void draw_tetromino(const struct Tetromino &t)
{
  for (int dx=0; dx<4; ++dx)
    for (int dy=0; dy<4; ++dy)
      dpy.setPoint(DPY_WIDTH-t.x-1-dx, DPY_HEIGHT-t.y-1-dy, t.get_pos(dx, dy));
}

void draw_stack()
{
  for (int x=0; x<DPY_WIDTH; ++x)
    for (int y=0; y<DPY_HEIGHT; ++y)
      if (stack[x][y])
        dpy.setPoint(DPY_WIDTH-x-1, DPY_HEIGHT-y-1, true);
}

bool is_pos_free(int x, int y)
{
  return 0 <= x && x < DPY_WIDTH &&
    0 <= y && y < DPY_HEIGHT &&
    !stack[x][y];
}

// TODO: make method of Tetromino
bool can_have_tetromino(const struct Tetromino &t)
{
  for (int dx=0; dx<4; ++dx)
    for (int dy=0; dy<4; ++dy)
      if (t.get_pos(dx, dy) && !is_pos_free(t.x+dx, t.y+dy))
        return false;
  return true;
}

// TODO: make method of Tetromino
void place_piece()
{
  for (int dx=0; dx<4; ++dx)
      for (int dy=0; dy<4; ++dy)
        if (tetromino.get_pos(dx, dy))
          stack[tetromino.x+dx][tetromino.y+dy] = true;
  for (int y=DPY_HEIGHT-1, offset=0; 0 <= y; --y)
  {
    if (stack[0][y] && stack[0][y] == stack[1][y] && stack[1][y] == stack[2][y] && stack[2][y] == stack[3][y] &&
          stack[3][y] == stack[4][y] && stack[4][y] == stack[5][y] && stack[5][y] == stack[6][y] && stack[6][y] == stack[7][y])
      ++offset;
    else
      for (int x=0; x<DPY_WIDTH; ++x)
        stack[x][y+offset] = stack[x][y];
  }
  tetromino.x = tetromino.y = tetromino.rotation = 0;
  tetromino.type = (tetromino.type + 1) % num_tetrominoes;
}

// TODO: make method of Tetromino
void apply_gravity()
{
  static unsigned long last_update = millis();
  unsigned long now = millis();
  if (now - last_update < 1000)
    return;
  last_update = now;
  
  if (can_have_tetromino(tetromino))
    ++tetromino.y;
  else
    place_piece();
  redraw();
}

void redraw()
{
  dpy.clear();
  draw_tetromino(tetromino);
  draw_stack();
}

void setup()
{
  Serial.begin(9600);
  dpy.begin();
  enc.begin();
  redraw();
}

void loop()
{
  if (!is_gravity_enabled && get_pressed_key(KEYPAD_OUT_PIN) != Key::None)
    is_gravity_enabled = true;
  else
    switch (get_pressed_key(KEYPAD_OUT_PIN))
    {
      case Key::Up:
      {
        int new_piece_rot = (tetromino.rotation + 1) % num_rotations;
        if (can_have_tetromino(Tetromino(tetromino.x, tetromino.y, tetromino.type, new_piece_rot)))
        {
          tetromino.rotation = new_piece_rot;
          redraw();
        }     
      } break;
      case Key::Down:
        if (can_have_tetromino(Tetromino(tetromino.x, tetromino.y+1, tetromino.type, tetromino.rotation)))
        {
          ++tetromino.y;
          redraw();
        }
        break;
      case Key::Left:
        if (can_have_tetromino(Tetromino(tetromino.x-1, tetromino.y, tetromino.type, tetromino.rotation)))
        {
          --tetromino.x;
          redraw();
        }
        break;
      case Key::Right:
        if (can_have_tetromino(Tetromino(tetromino.x+1, tetromino.y, tetromino.type, tetromino.rotation)))
        {
          ++tetromino.x;
          redraw();
        }
        break;
      case Key::Drop:
        while (can_have_tetromino(Tetromino(tetromino.x, tetromino.y+1, tetromino.type, tetromino.rotation)))
            ++tetromino.y;
        place_piece();
        break;
    }
  enc.read();
  if (enc.has_rotated)
    Serial.println(enc.wrapped_rotation(num_tetrominoes));
  if (enc.was_button_pressed)
    ((void (*)())0)(); // reset arduino
  if (is_gravity_enabled)
    apply_gravity();
  delay(1);
}
