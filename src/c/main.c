#include <pebble.h>

#define MAX_TITLE_LEN 56 
#define CHUNK_SIZE 4
#define MAX_SECTIONS 10
#define PERSIST_KEY_COUNT 100
#define PERSIST_KEY_TIME_24H 101
#define PERSIST_BASE_DATA_KEY 102 

typedef struct __attribute__((__packed__)) {
  uint32_t timestamp;
  uint32_t end_timestamp;
  char title[MAX_TITLE_LEN];
} CalendarEvent;

typedef struct {
  int start_index;
  int count;
  char date_text[32];
} CalendarSection;

static Window *s_main_window;
static Window *s_detail_window = NULL;
static MenuLayer *s_menu_layer;
static TextLayer *s_detail_title_layer, *s_detail_time_layer;

static CalendarEvent *s_event_array = NULL;
static CalendarEvent *s_new_data_buffer = NULL; 
static int s_total_expected_events = 0, s_events_received_so_far = 0, s_num_sections = 0;
static int s_selected_event_index = 0, s_use_24h = 0;
static CalendarSection s_sections[MAX_SECTIONS];

// --- 1. Basic Helpers (Must be at the top) ---

static void format_time(char *buffer, int buf_size, time_t timestamp) {
  struct tm *t = localtime(&timestamp);
  if (s_use_24h == 1) {
    strftime(buffer, buf_size, "%H:%M", t);
  } else {
    int hour = t->tm_hour;
    const char *am_pm = (hour >= 12) ? "PM" : "AM";
    hour = hour % 12;
    if (hour == 0) hour = 12;
    snprintf(buffer, buf_size, "%d:%02d%s", hour, t->tm_min, am_pm);
  }
}

static bool is_same_day(time_t t1, time_t t2) {
  struct tm *tm1 = localtime(&t1);
  int y1 = tm1->tm_year, d1 = tm1->tm_yday;
  struct tm *tm2 = localtime(&t2);
  return (y1 == tm2->tm_year && d1 == tm2->tm_yday);
}

// --- 2. Update Sections (Moved up so sharding functions can see it) ---

static void update_sections() {
  if (s_total_expected_events == 0 || !s_event_array) return;
  s_num_sections = 0;
  int current_start = 0;
  time_t now = time(NULL);
  for (int i = 0; i < s_total_expected_events; i++) {
    time_t et = (time_t)s_event_array[i].timestamp;
    if (i == 0 || !is_same_day((time_t)s_event_array[i-1].timestamp, et)) {
      if (s_num_sections > 0) s_sections[s_num_sections - 1].count = i - current_start;
      if (s_num_sections < MAX_SECTIONS) {
        s_sections[s_num_sections].start_index = i;
        strftime(s_sections[s_num_sections].date_text, 32, 
                 is_same_day(now, et) ? "Today" : 
                 is_same_day(now + 86400, et) ? "Tomorrow" : "%A, %b %d", 
                 localtime(&et));
        current_start = i;
        s_num_sections++;
      }
    }
  }
  if (s_num_sections > 0) s_sections[s_num_sections - 1].count = s_total_expected_events - current_start;
}

// --- 3. Sharded Persistence ---

static void save_data_to_storage() {
  persist_write_int(PERSIST_KEY_COUNT, s_total_expected_events);
  for (int i = 0; i < s_total_expected_events; i += CHUNK_SIZE) {
    int remaining = s_total_expected_events - i;
    int to_save = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
    persist_write_data(PERSIST_BASE_DATA_KEY + (i / CHUNK_SIZE), &s_event_array[i], to_save * sizeof(CalendarEvent));
  }
}

static void load_data_from_storage() {
  s_total_expected_events = persist_read_int(PERSIST_KEY_COUNT);
  if (s_total_expected_events > 0) {
    if (s_event_array) free(s_event_array);
    s_event_array = malloc(s_total_expected_events * sizeof(CalendarEvent));
    for (int i = 0; i < s_total_expected_events; i += CHUNK_SIZE) {
      int remaining = s_total_expected_events - i;
      int to_read = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
      persist_read_data(PERSIST_BASE_DATA_KEY + (i / CHUNK_SIZE), &s_event_array[i], to_read * sizeof(CalendarEvent));
    }
    update_sections();
  }
}

// --- 4. Detail View ---

static void detail_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_frame(root);
  window_set_background_color(window, GColorWhite);
  
  s_detail_time_layer = text_layer_create(GRect(10, 5, b.size.w - 20, 70));
  text_layer_set_font(s_detail_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_color(s_detail_time_layer, GColorBlack);
  layer_add_child(root, text_layer_get_layer(s_detail_time_layer));
  
  s_detail_title_layer = text_layer_create(GRect(10, 75, b.size.w - 20, 140));
  text_layer_set_font(s_detail_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(s_detail_title_layer, GColorBlack);
  text_layer_set_overflow_mode(s_detail_title_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_detail_title_layer));

  if (s_event_array) {
    // Ensuring title is set from the array
    text_layer_set_text(s_detail_title_layer, s_event_array[s_selected_event_index].title);
    
    static char when[64], s_b[16], e_b[16], d_b[16];
    time_t st = (time_t)s_event_array[s_selected_event_index].timestamp;
    strftime(d_b, 16, "%a, %b %d", localtime(&st));
    format_time(s_b, 16, st);
    format_time(e_b, 16, (time_t)s_event_array[s_selected_event_index].end_timestamp);
    snprintf(when, 64, "%s\n%s - %s", d_b, s_b, e_b);
    text_layer_set_text(s_detail_time_layer, when);
  }
}

static void detail_unload(Window *window) {
  text_layer_destroy(s_detail_title_layer); 
  text_layer_destroy(s_detail_time_layer);
}

static void show_detail(int index) {
  s_selected_event_index = index;
  if (!s_detail_window) {
    s_detail_window = window_create();
    window_set_window_handlers(s_detail_window, (WindowHandlers) {.load = detail_load, .unload = detail_unload});
  }
  window_stack_push(s_detail_window, true);
}

// --- 5. Menu Callbacks ---

static uint16_t get_num_sections_callback(MenuLayer *m, void *d) { return (s_num_sections > 0) ? s_num_sections : 1; }
static uint16_t get_num_rows_callback(MenuLayer *m, uint16_t s, void *d) { return (s_num_sections == 0) ? 1 : s_sections[s].count; }
static int16_t get_head_h_callback(MenuLayer *m, uint16_t s, void *d) { return (s_num_sections > 0) ? 23 : 0; }
static int16_t get_cell_h_callback(MenuLayer *m, MenuIndex *i, void *d) { return 48; }

static void draw_head_callback(GContext* ctx, const Layer *l, uint16_t s, void *d) {
  graphics_context_set_fill_color(ctx, GColorDukeBlue); 
  graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_sections[s].date_text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(5, 2, 200, 20), 0, GTextAlignmentLeft, NULL);
}

static void draw_row_callback(GContext* ctx, const Layer *l, MenuIndex *i, void *d) {
  if (s_num_sections == 0) { menu_cell_basic_draw(ctx, l, "Loading...", NULL, NULL); return; }
  CalendarEvent ev = s_event_array[s_sections[i->section].start_index + i->row];
  static char t_b[16]; format_time(t_b, 16, ev.timestamp);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, t_b, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(5, 0, 190, 20), 0, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, ev.title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(5, 16, 190, 30), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void select_click_callback(MenuLayer *m, MenuIndex *i, void *d) { 
  if(s_num_sections > 0) show_detail(s_sections[i->section].start_index + i->row); 
}

// --- 6. Comms ---

static void inbox_handler(DictionaryIterator *iter, void *ctx) {
  Tuple *f_t = dict_find(iter, MESSAGE_KEY_TIME_FORMAT);
  if (f_t) { 
    s_use_24h = (strcmp(f_t->value->cstring, "24h") == 0); 
    persist_write_int(PERSIST_KEY_TIME_24H, s_use_24h); 
    menu_layer_reload_data(s_menu_layer); 
  }
  Tuple *t_t = dict_find(iter, MESSAGE_KEY_TOTAL_EVENTS);
  Tuple *c_t = dict_find(iter, MESSAGE_KEY_CHUNK_INDEX);
  Tuple *d_t = dict_find(iter, MESSAGE_KEY_EVENT_DATA);
  if (t_t && c_t && d_t) {
    int total = t_t->value->int32;
    int chunk = c_t->value->int32;
    if (chunk == 0) {
      if (s_new_data_buffer) { free(s_new_data_buffer); s_new_data_buffer = NULL; }
      s_new_data_buffer = malloc(total * sizeof(CalendarEvent));
      s_events_received_so_far = 0;
    }
    if (s_new_data_buffer) {
      memcpy(&s_new_data_buffer[chunk * CHUNK_SIZE], d_t->value->data, d_t->length);
      s_events_received_so_far += (d_t->length / sizeof(CalendarEvent));
      if (s_events_received_so_far >= total) {
        if (s_event_array) { free(s_event_array); s_event_array = NULL; }
        s_event_array = s_new_data_buffer;
        s_new_data_buffer = NULL; 
        s_total_expected_events = total;
        update_sections(); 
        menu_layer_reload_data(s_menu_layer);
        save_data_to_storage();
      }
    }
  }
}

// --- 7. Lifecycle ---

static void main_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_menu_layer = menu_layer_create(layer_get_frame(window_layer));
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorCyan, GColorBlack); 

  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = get_num_sections_callback,
    .get_num_rows = get_num_rows_callback,
    .get_header_height = get_head_h_callback,
    .get_cell_height = get_cell_h_callback,
    .draw_header = draw_head_callback,
    .draw_row = draw_row_callback,
    .select_click = select_click_callback
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void main_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
}

static void init() {
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_load, 
    .unload = main_unload
  });
  if (persist_exists(PERSIST_KEY_TIME_24H)) s_use_24h = persist_read_int(PERSIST_KEY_TIME_24H);
  if (persist_exists(PERSIST_KEY_COUNT)) load_data_from_storage();
  
  window_stack_push(s_main_window, true);
  app_message_register_inbox_received(inbox_handler);
  app_message_open(2048, 512);

  if (s_total_expected_events == 0) {
    DictionaryIterator *out;
    app_message_outbox_begin(&out);
    dict_write_uint8(out, 0, 1);
    app_message_outbox_send();
  }
}

static void deinit() {
  window_destroy(s_main_window);
  if (s_detail_window) window_destroy(s_detail_window);
  if (s_event_array) { free(s_event_array); s_event_array = NULL; }
  if (s_new_data_buffer) { free(s_new_data_buffer); s_new_data_buffer = NULL; }
}

int main(void) { init(); app_event_loop(); deinit(); }