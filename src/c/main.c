// Include the standard Pebble SDK library for watchface and app development
#include <pebble.h>

// --- Configuration Constants ---
// Define the maximum length for a calendar event title string (55 characters + 1 null terminator)
#define MAX_TITLE_LEN 56 
// Define how many events are sent in a single Bluetooth message to avoid hitting limits
#define CHUNK_SIZE 2
#define MAX_EVENTS 48
#define STORAGE_VERSION 3
#define PERSIST_KEY_VERSION 99
#define PERSIST_KEY_SELECTION 98
#define PERSIST_KEY_FONT_SIZE 97
// Define the maximum number of distinct day groups (e.g., Today, Tomorrow)
#define MAX_SECTIONS 16
#define PERSIST_KEY_THEME 94
#define PERSIST_KEY_SHOW_PAST 95
#define PERSIST_KEY_STATUS_BAR 96
#define PERSIST_KEY_TIME_MODE 93
#define PERSIST_KEY_LAST_SYNC 92
#define PERSIST_KEY_SUNRISE 90
#define PERSIST_KEY_SUNSET 91
// Define the storage key used to save the total number of events
#define PERSIST_KEY_COUNT 100
// Define the storage key used to save the user's 12h/24h time format preference
#define PERSIST_KEY_TIME_24H 101
// Define the starting storage key for our event data (102, 103, 104...) to bypass the 256-byte limit
#define PERSIST_BASE_DATA_KEY 102 

// --- Data Structures ---
// Define the structure of a single calendar event. '__packed__' ensures no wasted memory padding.
typedef struct __attribute__((__packed__)) {
  uint32_t timestamp;     // The start time of the event in Unix Epoch seconds
  uint32_t end_timestamp; // The end time of the event in Unix Epoch seconds
  char title[MAX_TITLE_LEN]; // The title of the event
  uint8_t color; // Calendar source color (1..6)
} CalendarEvent;

// Define a structure to group events into days (Sections) for the MenuLayer
typedef struct {
  int start_index;   // The index in the main array where this day's events begin
  int count;         // How many events fall on this specific day
  char date_text[32]; // The display text for the header (e.g., "Today", "Tomorrow")
} CalendarSection;

// --- Global UI Variables ---
static Window *s_main_window;           // The main application window
static Window *s_detail_window = NULL;  // The window that pops up to show event details
static MenuLayer *s_menu_layer;
static Window *s_settings_window;
static MenuLayer *s_settings_menu;
static StatusBarLayer *s_detail_status_bar;         // The scrolling list layer for the main window
static ScrollLayer *s_detail_scroll_layer;
static TextLayer *s_detail_title_layer; // The text layer showing the event title in the detail window
static TextLayer *s_detail_time_layer;
static TextLayer *s_detail_meta_layer;
static char s_detail_meta[360] = "Loading details...";
static char s_detail_title_copy[MAX_TITLE_LEN];
static AppTimer *s_detail_timeout=NULL;
static int32_t s_detail_request_token=0;  // The text layer showing the event time in the detail window
static TextLayer *s_toast_layer;        // The notification layer for the "Syncing..." status

// --- Global Data Variables ---
static CalendarEvent *s_event_array = NULL;     // Pointer to the memory holding the currently displayed events
static CalendarEvent *s_new_data_buffer = NULL; // Pointer to the memory holding incoming data during a sync
static int s_total_expected_events = 0;         // The total number of events the phone says it will send
static int s_events_received_so_far = 0;
static int s_next_chunk = 0;
static bool s_sync_in_progress = false;        // Counter tracking how many events have successfully arrived
static int s_num_sections = 0;                  // How many unique day headers we currently have
static int s_selected_event_index = 0;
static int s_restore_event_index = 0;
static PreferredContentSize s_content_size = PreferredContentSizeMedium;
static int s_font_mode = 0; // 0=System, 1=Small, 2=Medium, 3=Large
          // The index of the event the user clicked on
static CalendarSection s_sections[MAX_SECTIONS];
static int s_visible_indices[MAX_EVENTS];
static int s_visible_count = 0;
static int s_theme = 0; // 0=Auto day/night, 1=Light, 2=Dark
static time_t s_sunrise = 0, s_sunset = 0;
static bool s_show_past = false;
static bool s_show_status_bar = false;
static int s_time_mode = 0; // 0=System, 1=12h, 2=24h
static time_t s_last_sync = 0;
static uint32_t s_active_sync_id = 0, s_completed_sync_id = 0;
static uint32_t s_transfer_checksum = 1, s_expected_checksum = 1;
static int s_ack_chunk = -1;
static uint32_t s_ack_id = 0;
static int s_ack_attempts = 0;
static AppTimer *s_ack_timer = NULL;
static AppTimer *s_sync_watchdog = NULL;
static AppTimer *s_pref_timer = NULL;// Array holding the grouping data for the Menu headers

// --- Helper Functions ---
// Muted source markers; monochrome watches use black bars.
static GColor event_color(uint8_t color) {
#ifdef PBL_COLOR
  switch (color) {
    case 2: return GColorRed;
    case 3: return GColorGreen;
    case 4: return GColorYellow;
    case 5: return GColorOrange;
    case 6: return GColorPurple;
    default: return GColorBlue;
  }
#else
  return GColorBlack;
#endif
}



// Formats a Unix timestamp into a human-readable time string (12h or 24h)
static void format_time(char *buffer, int buf_size, time_t timestamp) {
  struct tm *t = localtime(&timestamp); // Convert the Unix timestamp to a local time structure
  if (s_time_mode == 2 || (s_time_mode == 0 && clock_is_24h_style())) { // System or explicit 24-hour
    strftime(buffer, buf_size, "%H:%M", t); // Format as HH:MM (e.g., 14:30)
  } else { // If the user prefers 12-hour time
    int hour = t->tm_hour; // Extract the hour (0-23)
    const char *am_pm = (hour >= 12) ? "PM" : "AM"; // Determine if it is AM or PM
    hour = hour % 12; // Convert 13-23 to 1-11
    if (hour == 0) hour = 12; // Midnight (0) and Noon (12) should display as 12
    snprintf(buffer, buf_size, "%d:%02d%s", hour, t->tm_min, am_pm); // Format as H:MMAM/PM (e.g., 2:30PM)
  }
}

// Checks if two Unix timestamps occur on the exact same calendar day
static bool is_same_day(time_t t1, time_t t2) {
  struct tm *tm1 = localtime(&t1); // Convert first time to local structure
  int y1 = tm1->tm_year; // Extract the year
  int d1 = tm1->tm_yday; // Extract the day of the year (0-365)
  struct tm *tm2 = localtime(&t2); // Convert second time to local structure
  return (y1 == tm2->tm_year && d1 == tm2->tm_yday); // Return true if both year and day match
}

// Organizes the linear list of events into groups based on their calendar day
// The stored event array is immutable during rendering. A visible-index map
// filters completed events without losing data or disturbing detail lookup IDs.
static void update_sections(void) {
  s_num_sections = 0;
  s_visible_count = 0;
  if (!s_event_array || s_total_expected_events <= 0) return;
  time_t now = time(NULL);
  struct tm tomorrow_tm = *localtime(&now);
  tomorrow_tm.tm_mday += 1;
  tomorrow_tm.tm_isdst = -1;
  time_t tomorrow = mktime(&tomorrow_tm);
  for (int i = 0; i < s_total_expected_events && s_visible_count < MAX_EVENTS; i++) {
    CalendarEvent *event = &s_event_array[i];
    if (!s_show_past && (time_t)event->end_timestamp < now) continue;
    time_t date = (time_t)event->timestamp;
    bool new_day = s_num_sections == 0;
    if (!new_day) {
      int previous = s_visible_indices[s_visible_count - 1];
      new_day = !is_same_day((time_t)s_event_array[previous].timestamp, date);
    }
    if (new_day) {
      if (s_num_sections >= MAX_SECTIONS) break;
      CalendarSection *section = &s_sections[s_num_sections++];
      section->start_index = s_visible_count;
      section->count = 0;
      strftime(section->date_text, sizeof(section->date_text),
        is_same_day(now, date) ? "Today" :
        is_same_day(tomorrow, date) ? "Tomorrow" : "%a, %b %d",
        localtime(&date));
    }
    s_visible_indices[s_visible_count++] = i;
    s_sections[s_num_sections - 1].count++;
  }
}
// In Auto, use today's weather-plugin sunrise and sunset if available.
// Never use yesterday's solar times: they are invalid after local midnight.
static bool solar_times_valid(time_t now) {
  if (s_sunrise <= 0 || s_sunset <= s_sunrise) return false;
  if (s_sunset - s_sunrise > 24 * 60 * 60) return false;
  struct tm sunrise_day = *localtime(&s_sunrise);
  struct tm now_day = *localtime(&now);
  return sunrise_day.tm_year == now_day.tm_year &&
         sunrise_day.tm_yday == now_day.tm_yday;
}
static bool dark_mode(void) {
  if (s_theme == 1) return false;
  if (s_theme == 2) return true;
  time_t now = time(NULL);
  if (solar_times_valid(now)) return now < s_sunrise || now >= s_sunset;
  // No Weather plugin, denied Location access, or stale forecast.
  struct tm *t = localtime(&now);
  return t && (t->tm_hour < 7 || t->tm_hour >= 19);
}
static GColor ui_background(void) { return dark_mode() ? GColorBlack : GColorWhite; }
static GColor ui_foreground(void) { return dark_mode() ? GColorWhite : GColorBlack; }

static int effective_font_mode(void) {
  if (s_font_mode >= 1 && s_font_mode <= 3) return s_font_mode;
  return (s_content_size >= PreferredContentSizeLarge) ? 3 :
         (s_content_size == PreferredContentSizeSmall) ? 1 : 2;
}
static int row_height(void) {
  switch (effective_font_mode()) {
    case 1: return 40;
    case 3: return 64;
    default: return 49;
  }
}
static const char *event_font(void) {
  switch (effective_font_mode()) {
    case 1: return FONT_KEY_GOTHIC_18_BOLD;
    case 3: return FONT_KEY_GOTHIC_28_BOLD;
    default: return FONT_KEY_GOTHIC_24_BOLD;
  }
}
static const char *time_font(void) {
  return effective_font_mode() == 1 ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_18;
}
static void refresh_glance(void);
static void restore_selection(void) {
  if (!s_menu_layer || s_total_expected_events <= 0) return;
  int index = s_restore_event_index;
  if (index >= s_total_expected_events) index = s_total_expected_events - 1;
  if (index < 0) index = 0;
  for (int section = 0; section < s_num_sections; section++) {
    for (int row = 0; row < s_sections[section].count; row++) {
      if (s_visible_indices[s_sections[section].start_index + row] == index) {
        menu_layer_set_selected_index(s_menu_layer, (MenuIndex){.section=section,.row=row}, MenuRowAlignCenter, false);
        return;
      }
    }
  }
}
#if !PBL_PLATFORM_APLITE
static void glance_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
  if (!limit) return;
  char subtitle[96] = "No upcoming events";
  time_t now = time(NULL);
  if (s_event_array) {
    for (int i=0; i<s_total_expected_events; i++) {
      if ((time_t)s_event_array[i].end_timestamp < now) continue;
      char start[16];
      format_time(start, sizeof(start), (time_t)s_event_array[i].timestamp);
      snprintf(subtitle, sizeof(subtitle), "%s %s", start, s_event_array[i].title);
      break;
    }
  }
  AppGlanceSlice slice = {
    .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
    .layout = {.icon=APP_GLANCE_SLICE_DEFAULT_ICON,.subtitle_template_string=subtitle}
  };
  app_glance_add_slice(session, slice);
}
#endif
static void refresh_glance(void) {
#if !PBL_PLATFORM_APLITE
  app_glance_reload(glance_callback, NULL);
#endif
}

// --- Persistence Functions ---

// Saves the event array into the watch's permanent memory in small chunks
static void save_data_to_storage() {
  persist_write_int(PERSIST_KEY_VERSION, STORAGE_VERSION);
  persist_write_int(PERSIST_KEY_COUNT, s_total_expected_events); // Save the total number of events
  
  // Loop through the array, jumping forward by CHUNK_SIZE (4) each time
  for (int i = 0; i < s_total_expected_events; i += CHUNK_SIZE) {
    int remaining = s_total_expected_events - i; // Calculate how many events are left
    int to_save = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE; // Determine if we save a full chunk of 4, or a partial chunk
    
    // Save the chunk to a unique key (102, 103, 104, etc.) to bypass the 256-byte limit
    if (persist_write_data(PERSIST_BASE_DATA_KEY + (i / CHUNK_SIZE), &s_event_array[i], to_save * sizeof(CalendarEvent)) != (int)(to_save * sizeof(CalendarEvent))) {
      persist_delete(PERSIST_KEY_VERSION);
      return;
    }
  }
}

// Loads the event array from the watch's permanent memory
static void load_data_from_storage() {
  if (!persist_exists(PERSIST_KEY_VERSION) ||
      persist_read_int(PERSIST_KEY_VERSION) != STORAGE_VERSION) return;
  if (!persist_exists(PERSIST_KEY_COUNT)) return;
  int total = persist_read_int(PERSIST_KEY_COUNT);
  if (total <= 0 || total > MAX_EVENTS) return;
  CalendarEvent *loaded = calloc(total, sizeof(CalendarEvent));
  if (!loaded) return;
  for (int i = 0; i < total; i += CHUNK_SIZE) {
    int count = (total - i < CHUNK_SIZE) ? total - i : CHUNK_SIZE;
    int expected = count * sizeof(CalendarEvent);
    if (persist_read_data(PERSIST_BASE_DATA_KEY + (i / CHUNK_SIZE), &loaded[i], expected) != expected) {
      free(loaded);
      return;
    }
    for (int j = i; j < i + count; j++) {
      if (!memchr(loaded[j].title, 0, MAX_TITLE_LEN)) {
        free(loaded);
        return;
      }
    }
  }
  s_event_array = loaded;
  s_total_expected_events = total;
  update_sections();
}

// --- Detail Window Functions ---

// Sets up the UI elements when the Detail Window is opened
// Measure text with its final width, then stack the layers without overlap.
static void layout_detail_content(void) {
  if (!s_detail_scroll_layer || !s_detail_time_layer ||
      !s_detail_title_layer || !s_detail_meta_layer) return;
  GRect bounds = layer_get_bounds(window_get_root_layer(s_detail_window));
  int margin = PBL_IF_ROUND_ELSE(22, 10);
  int width = bounds.size.w - 2 * margin;
  int y = PBL_IF_ROUND_ELSE(22, 8);
  TextLayer *layers[] = {s_detail_time_layer, s_detail_title_layer, s_detail_meta_layer};
  for (int i = 0; i < 3; i++) {
    // This generous temporary height prevents measurement from clipping lines.
    layer_set_frame(text_layer_get_layer(layers[i]), GRect(margin, y, width, 1200));
    GSize size = text_layer_get_content_size(layers[i]);
    int height = size.h + 4;
    layer_set_frame(text_layer_get_layer(layers[i]), GRect(margin, y, width, height));
    y += height + (i == 0 ? 12 : 10);
  }
  y += PBL_IF_ROUND_ELSE(22, 12);
  if (y < bounds.size.h - (s_show_status_bar ? 16 : 0)) y = bounds.size.h - (s_show_status_bar ? 16 : 0);
  scroll_layer_set_content_size(s_detail_scroll_layer, GSize(bounds.size.w, y));
}
static void detail_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  window_set_background_color(window, ui_background());
  s_detail_scroll_layer = scroll_layer_create(GRect(0, s_show_status_bar ? 16 : 0, bounds.size.w, bounds.size.h - (s_show_status_bar ? 16 : 0)));
  if (s_show_status_bar) {
    s_detail_status_bar = status_bar_layer_create();
    status_bar_layer_set_colors(s_detail_status_bar, ui_background(), ui_foreground());
    layer_add_child(root, status_bar_layer_get_layer(s_detail_status_bar));
  }
  scroll_layer_set_click_config_onto_window(s_detail_scroll_layer, window);
  layer_add_child(root, scroll_layer_get_layer(s_detail_scroll_layer));
  const int font = effective_font_mode();
  s_detail_time_layer = text_layer_create(GRect(0, 0, bounds.size.w, 48));
  text_layer_set_font(s_detail_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(s_detail_time_layer, ui_foreground());
  text_layer_set_background_color(s_detail_time_layer, GColorClear);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_time_layer));
  s_detail_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, 48));
  text_layer_set_font(s_detail_title_layer, fonts_get_system_font(
    font == 3 ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_color(s_detail_title_layer, ui_foreground());
  text_layer_set_background_color(s_detail_title_layer, GColorClear);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_title_layer));
  s_detail_meta_layer = text_layer_create(GRect(0, 0, bounds.size.w, 48));
  text_layer_set_font(s_detail_meta_layer, fonts_get_system_font(
    font == 1 ? FONT_KEY_GOTHIC_18 : (font == 3 ? FONT_KEY_GOTHIC_28 : FONT_KEY_GOTHIC_24)));
  text_layer_set_text_color(s_detail_meta_layer, ui_foreground());
  text_layer_set_background_color(s_detail_meta_layer, GColorClear);
  text_layer_set_text(s_detail_meta_layer, s_detail_meta);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_meta_layer));
  if (s_event_array && s_selected_event_index < s_total_expected_events) {
    CalendarEvent *ev = &s_event_array[s_selected_event_index];
    snprintf(s_detail_title_copy, sizeof(s_detail_title_copy), "%s", ev->title);
    text_layer_set_text(s_detail_title_layer, s_detail_title_copy);
    static char when[64], begin[16], finish[16], date[20];
    time_t start = (time_t)ev->timestamp;
    strftime(date, sizeof(date), "%a, %b %d", localtime(&start));
    format_time(begin, sizeof(begin), start);
    format_time(finish, sizeof(finish), (time_t)ev->end_timestamp);
    snprintf(when, sizeof(when), "%s\n%s - %s", date, begin, finish);
    text_layer_set_text(s_detail_time_layer, when);
  }
  layout_detail_content();
}
static void detail_unload(Window *window) {
  if (s_detail_timeout) { app_timer_cancel(s_detail_timeout); s_detail_timeout=NULL; }
  text_layer_destroy(s_detail_title_layer);
  text_layer_destroy(s_detail_time_layer);
  text_layer_destroy(s_detail_meta_layer);
  s_detail_meta_layer = NULL;
  scroll_layer_destroy(s_detail_scroll_layer);
  if (s_detail_status_bar) { status_bar_layer_destroy(s_detail_status_bar); s_detail_status_bar = NULL; }
}

static void detail_timeout_callback(void *context) {
  s_detail_timeout=NULL;
  if (!s_detail_meta_layer || strcmp(s_detail_meta, "Loading details...")!=0) return;
  snprintf(s_detail_meta, sizeof(s_detail_meta), "Details unavailable offline.\\nThe event list is saved on your watch.");
  text_layer_set_text(s_detail_meta_layer, s_detail_meta);
  layout_detail_content();
}

// Opens the detail window for a specific event index
static void show_detail(int index) {
  if (!s_event_array || index < 0 || index >= s_total_expected_events) return;
  if (s_detail_timeout) { app_timer_cancel(s_detail_timeout); s_detail_timeout=NULL; }
  snprintf(s_detail_meta, sizeof(s_detail_meta), "Loading details...");
  s_detail_request_token++;
  s_selected_event_index = index; // Save the clicked index to the global variable
  if (!s_detail_window) { // If the window doesn't exist yet, create it
    s_detail_window = window_create();
    window_set_window_handlers(s_detail_window, (WindowHandlers) {
      .load = detail_load,   // Set the load handler
      .unload = detail_unload // Set the unload handler
    });
  }
  window_stack_push(s_detail_window, true); // Push the window onto the screen with an animation
  DictionaryIterator *request;
  if (app_message_outbox_begin(&request) == APP_MSG_OK) {
    dict_write_int32(request, MESSAGE_KEY_DETAIL_REQUEST, index);
    dict_write_int32(request, MESSAGE_KEY_DETAIL_TOKEN, s_detail_request_token);
    app_message_outbox_send();
  }
  s_detail_timeout=app_timer_register(8000,detail_timeout_callback,NULL);
}

// --- MenuLayer Callbacks ---

// Tells the MenuLayer how many headers (sections) to draw
static uint16_t get_num_sections_callback(MenuLayer *m, void *d) { 
  return (s_num_sections > 0) ? s_num_sections : 1; // Return 1 if loading, otherwise return actual section count
}

// Tells the MenuLayer how many rows belong in a specific section
static uint16_t get_num_rows_callback(MenuLayer *m, uint16_t s, void *d) { 
  return (s_num_sections == 0) ? 1 : s_sections[s].count; // Return 1 for the "Loading" row, otherwise return actual count
}

// Tells the MenuLayer how tall the header should be
static int16_t get_head_h_callback(MenuLayer *m, uint16_t s, void *d) { 
  return (s_num_sections > 0) ? 23 : 0; // Return 23 pixels if data exists, 0 if loading
}

// Tells the MenuLayer how tall each event row should be
static int16_t get_cell_h_callback(MenuLayer *m, MenuIndex *i, void *d) { 
  return row_height();
}

// Draws the visual design of the headers (Today, Tomorrow, etc.)
static void draw_head_callback(GContext* ctx, const Layer *l, uint16_t s, void *d) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDukeBlue, GColorBlack)); // Set background color to dark blue
  graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone); // Draw a rectangle filling the header bounds
  graphics_context_set_text_color(ctx, GColorWhite); // Set text color to white
  // Draw the date text using a bold 14pt font
  graphics_draw_text(ctx, s_sections[s].date_text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(5, 2, layer_get_bounds(l).size.w - 10, 20), 0, GTextAlignmentLeft, NULL);
}

// Draws the visual design of the individual event rows
static void draw_row_callback(GContext* ctx, const Layer *l, MenuIndex *i, void *d) {
  if (s_num_sections == 0) { // If there is no data...
    menu_cell_basic_draw(ctx, l, s_sync_in_progress ? "Loading..." : "No upcoming events", NULL, NULL); // ...draw a basic "Loading..." cell
    return; // Stop drawing
  }
  
  // Calculate which event this row corresponds to
  CalendarEvent ev = s_event_array[s_visible_indices[s_sections[i->section].start_index + i->row]];
  graphics_context_set_fill_color(ctx, event_color(ev.color));
  graphics_fill_rect(ctx, GRect(0, 3, 4, row_height() - 6), 0, GCornerNone);
  static char t_b[16]; 
  format_time(t_b, 16, ev.timestamp); // Format the start time
  
  bool highlighted = menu_cell_layer_is_highlighted(l);
  GColor selected_text = dark_mode() ? GColorBlack : PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite);
  graphics_context_set_text_color(ctx, highlighted ? selected_text : ui_foreground()); // Keep highlighted text readable
  // Draw the time text in the top half of the row
  graphics_draw_text(ctx, t_b, fonts_get_system_font(time_font()), GRect(9, 0, layer_get_bounds(l).size.w - 14, 20), 0, GTextAlignmentLeft, NULL);
  // Respect the user's system text size.
  graphics_draw_text(ctx, ev.title, fonts_get_system_font(event_font()), GRect(9, 16, layer_get_bounds(l).size.w - 14, row_height() - 17), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// Handles what happens when the user clicks the "Select" (middle) button on a row
static void select_click_callback(MenuLayer *m, MenuIndex *i, void *d) { 
  if(s_num_sections > 0) { // If actual data exists
    s_restore_event_index = s_visible_indices[s_sections[i->section].start_index + i->row];
    persist_write_int(PERSIST_KEY_SELECTION, s_restore_event_index);
    show_detail(s_visible_indices[s_sections[i->section].start_index + i->row]); // Open the detail window for that specific event
  }
}

// Handles what happens when the user LONG-PRESSES the "Select" (middle) button on a row
static void open_settings(void);
static void select_long_click_callback(MenuLayer *m, MenuIndex *i, void *d) {
  open_settings();
}

// --- Watch-side settings; no phone connection is needed to change UI options. ---
enum { SETTING_SYNC, SETTING_THEME, SETTING_FONT, SETTING_PAST,
       SETTING_TIME, SETTING_STATUS_BAR, SETTING_LAST_SYNC, SETTINGS_COUNT };

static void apply_main_theme(void) {
  if (!s_main_window || !s_menu_layer) return;
  window_set_background_color(s_main_window, ui_background());
  menu_layer_set_normal_colors(s_menu_layer, ui_background(), ui_foreground());
  menu_layer_set_highlight_colors(s_menu_layer,
    dark_mode() ? GColorWhite : PBL_IF_COLOR_ELSE(GColorCyan, GColorBlack),
    dark_mode() ? GColorBlack : PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
  menu_layer_reload_data(s_menu_layer);
  if (s_settings_menu) {
    window_set_background_color(s_settings_window, ui_background());
    menu_layer_set_normal_colors(s_settings_menu, ui_background(), ui_foreground());
    menu_layer_set_highlight_colors(s_settings_menu,
      dark_mode() ? GColorWhite : PBL_IF_COLOR_ELSE(GColorCyan, GColorBlack),
      dark_mode() ? GColorBlack : PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
    menu_layer_reload_data(s_settings_menu);
  }
}
static void sync_watchdog(void *context) {
  s_sync_watchdog=NULL;
  if (s_sync_in_progress && s_toast_layer) {
    text_layer_set_text(s_toast_layer, "Sync delayed (cached)");
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
  }
}
static void request_calendar_sync(bool force) {
  DictionaryIterator *out = NULL;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result == APP_MSG_OK && out) {
    dict_write_cstring(out, MESSAGE_KEY_SYNC_COMMAND, force ? "refresh" : "sync");
    result = app_message_outbox_send();
  }
  if (result == APP_MSG_OK) {
    if (s_sync_watchdog) app_timer_cancel(s_sync_watchdog);
    s_sync_watchdog=app_timer_register(60000,sync_watchdog,NULL);
    s_sync_in_progress = true;
    text_layer_set_text(s_toast_layer, "Syncing data...");
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
  } else {
    text_layer_set_text(s_toast_layer, "Phone unavailable");
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
  }
}
static uint16_t settings_rows(MenuLayer *menu, uint16_t section, void *data) {
  return SETTINGS_COUNT;
}
static void settings_draw(GContext *ctx, const Layer *layer, MenuIndex *index, void *data) {
  static char last[40];
  const char *title = "", *subtitle = "";
  switch (index->row) {
    case SETTING_SYNC: title="Force Sync"; subtitle="Fetch calendar now"; break;
    case SETTING_THEME:
      title="Appearance";
      subtitle=s_theme==1 ? "Light" : s_theme==2 ? "Dark" : "Auto (day/night)";
      break;
    case SETTING_FONT:
      title="Font Size";
      subtitle=s_font_mode==1 ? "Small" : s_font_mode==2 ? "Medium" :
               s_font_mode==3 ? "Large" : "System";
      break;
    case SETTING_PAST:
      title="Past Events"; subtitle=s_show_past ? "Show" : "Hide"; break;
    case SETTING_TIME:
      title="Time Format";
      subtitle=s_time_mode==1 ? "12 hour" : s_time_mode==2 ? "24 hour" : "System";
      break;
    case SETTING_STATUS_BAR:
      title="Detail Status Bar"; subtitle=s_show_status_bar ? "On" : "Off"; break;
    case SETTING_LAST_SYNC:
      title="Last Updated";
      if (!s_last_sync) subtitle="Not yet synced";
      else {
        strftime(last, sizeof(last), "%a %b %d %H:%M", localtime(&s_last_sync));
        subtitle=last;
      }
      break;
  }
  menu_cell_basic_draw(ctx, layer, title, subtitle, NULL);
}
// Keep phone configuration in step with preferences changed directly on-watch.
static void send_watch_preferences(void) {
  DictionaryIterator *out=NULL;
  if (app_message_outbox_begin(&out) != APP_MSG_OK || !out) return;
  char theme[4], font[4], past[4], bar[4];
  snprintf(theme,sizeof(theme),"%d",s_theme);
  snprintf(font,sizeof(font),"%d",s_font_mode);
  snprintf(past,sizeof(past),"%d",s_show_past?1:0);
  snprintf(bar,sizeof(bar),"%d",s_show_status_bar?1:0);
  dict_write_uint8(out, MESSAGE_KEY_WATCH_PREFS, 1);
  dict_write_cstring(out, MESSAGE_KEY_THEME_MODE, theme);
  dict_write_cstring(out, MESSAGE_KEY_FONT_SIZE, font);
  dict_write_cstring(out, MESSAGE_KEY_SHOW_PAST, past);
  dict_write_cstring(out, MESSAGE_KEY_DETAIL_STATUS_BAR, bar);
  dict_write_cstring(out, MESSAGE_KEY_TIME_FORMAT,
    s_time_mode==1 ? "12h" : s_time_mode==2 ? "24h" : "System");
  app_message_outbox_send();
}
static void pending_pref_sync(void *context) {
  s_pref_timer=NULL;
  send_watch_preferences();
}
static void schedule_pref_sync(void) {
  if (s_pref_timer) app_timer_cancel(s_pref_timer);
  s_pref_timer=app_timer_register(500,pending_pref_sync,NULL);
}
static void settings_select(MenuLayer *menu, MenuIndex *index, void *data) {
  switch (index->row) {
    case SETTING_SYNC:
      request_calendar_sync(true);
      window_stack_pop(true);
      break;
    case SETTING_THEME:
      s_theme = (s_theme + 1) % 3;
      persist_write_int(PERSIST_KEY_THEME, s_theme);
      apply_main_theme();
      break;
    case SETTING_FONT:
      s_font_mode = (s_font_mode + 1) % 4;
      persist_write_int(PERSIST_KEY_FONT_SIZE, s_font_mode);
      menu_layer_reload_data(s_menu_layer);
      break;
    case SETTING_PAST:
      s_show_past = !s_show_past;
      persist_write_bool(PERSIST_KEY_SHOW_PAST, s_show_past);
      update_sections();
      menu_layer_reload_data(s_menu_layer);
      restore_selection();
      break;
    case SETTING_TIME:
      s_time_mode = (s_time_mode + 1) % 3;
      persist_write_int(PERSIST_KEY_TIME_MODE, s_time_mode);
      menu_layer_reload_data(s_menu_layer);
      refresh_glance();
      break;
    case SETTING_STATUS_BAR:
      s_show_status_bar = !s_show_status_bar;
      persist_write_bool(PERSIST_KEY_STATUS_BAR, s_show_status_bar);
      break;
    default: break;
  }
  if (index->row != SETTING_SYNC && index->row != SETTING_LAST_SYNC)
    schedule_pref_sync();
  if (s_settings_menu) menu_layer_reload_data(s_settings_menu);
}
static void settings_load(Window *window) {
  Layer *root=window_get_root_layer(window);
  s_settings_menu=menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_settings_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows=settings_rows, .draw_row=settings_draw,
    .select_click=settings_select
  });
  menu_layer_set_click_config_onto_window(s_settings_menu, window);
  layer_add_child(root, menu_layer_get_layer(s_settings_menu));
  apply_main_theme();
}
static void settings_unload(Window *window) {
  menu_layer_destroy(s_settings_menu);
  s_settings_menu=NULL;
}
static void open_settings(void) {
  if (!s_settings_window) {
    s_settings_window=window_create();
    window_set_window_handlers(s_settings_window, (WindowHandlers){
      .load=settings_load, .unload=settings_unload
    });
  }
  window_stack_push(s_settings_window, true);
}
static void minute_tick(struct tm *tick_time, TimeUnits changed) {
  if (!s_main_window || !s_menu_layer) return;
  update_sections();
  apply_main_theme();
}

// --- Transactional AppMessage transport, using a watch-side ACK per chunk. ---
static uint32_t checksum_bytes(uint32_t checksum, const uint8_t *bytes, size_t length) {
  uint32_t a=checksum & 65535, b=(checksum >> 16) & 65535;
  for (size_t i=0; i<length; i++) {
    a=(a+bytes[i])%65521;
    b=(b+a)%65521;
  }
  return (b<<16)|a;
}
static void retry_ack(void *context);
static void send_ack(uint32_t id, int index) {
  if (s_ack_timer) { app_timer_cancel(s_ack_timer); s_ack_timer=NULL; }
  if (s_ack_id != id || s_ack_chunk != index) s_ack_attempts=0;
  s_ack_id=id;
  s_ack_chunk=index;
  DictionaryIterator *out=NULL;
  AppMessageResult result=app_message_outbox_begin(&out);
  if (result == APP_MSG_OK && out) {
    dict_write_int32(out, MESSAGE_KEY_SYNC_ID, (int32_t)id);
    dict_write_int32(out, MESSAGE_KEY_CHUNK_ACK, index);
    result=app_message_outbox_send();
  }
  if (result != APP_MSG_OK && s_ack_attempts++ < 5) {
    s_ack_timer=app_timer_register(250, retry_ack, NULL);
  }
}
static void retry_ack(void *context) {
  s_ack_timer=NULL;
  if (s_ack_chunk >= 0) send_ack(s_ack_id, s_ack_chunk);
}
static void ack_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  if (!iter || !dict_find(iter, MESSAGE_KEY_CHUNK_ACK)) return;
  if (s_ack_chunk >= 0 && !s_ack_timer && s_ack_attempts++ < 5)
    s_ack_timer=app_timer_register(300, retry_ack, NULL);
}
static void finish_calendar_sync(CalendarEvent *incoming, int total, uint32_t id) {
  if (s_event_array) free(s_event_array);
  s_event_array=incoming;
  s_new_data_buffer=NULL;
  s_total_expected_events=total;
  s_sync_in_progress=false;
  if (s_sync_watchdog) { app_timer_cancel(s_sync_watchdog); s_sync_watchdog=NULL; }
  s_completed_sync_id=id;
  s_active_sync_id=0;
  s_last_sync=time(NULL);
  persist_write_int(PERSIST_KEY_LAST_SYNC, (int)s_last_sync);
  update_sections();
  menu_layer_reload_data(s_menu_layer);
  restore_selection();
  save_data_to_storage();
  refresh_glance();
  layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
  if (s_settings_menu) menu_layer_reload_data(s_settings_menu);
}
static void inbox_handler(DictionaryIterator *iter, void *ctx) {
  Tuple *detail=dict_find(iter, MESSAGE_KEY_DETAIL_TEXT);
  if (detail && detail->type == TUPLE_CSTRING) {
    Tuple *token=dict_find(iter, MESSAGE_KEY_DETAIL_TOKEN);
    if (!token || token->value->int32 != s_detail_request_token) return;
    if (s_detail_timeout) { app_timer_cancel(s_detail_timeout); s_detail_timeout=NULL; }
    snprintf(s_detail_meta, sizeof(s_detail_meta), "%s", detail->value->cstring);
    if (s_detail_meta_layer) {
      text_layer_set_text(s_detail_meta_layer, s_detail_meta);
      layout_detail_content();
    }
    return;
  }
  Tuple *status=dict_find(iter, MESSAGE_KEY_SYNC_STATUS);
  if (status && status->type == TUPLE_CSTRING) {
    if (strcmp(status->value->cstring, "error") == 0) {
      s_sync_in_progress=false;
      if (s_sync_watchdog) { app_timer_cancel(s_sync_watchdog); s_sync_watchdog=NULL; }
      text_layer_set_text(s_toast_layer, "Sync failed (cached)");
      layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
      menu_layer_reload_data(s_menu_layer);
    }
    return;
  }
  Tuple *rise=dict_find(iter, MESSAGE_KEY_SUNRISE_TIME);
  Tuple *set=dict_find(iter, MESSAGE_KEY_SUNSET_TIME);
  if (rise && set) {
    time_t sunrise=(time_t)rise->value->int32;
    time_t sunset=(time_t)set->value->int32;
    // A zero/zero update explicitly disables outdated solar values.
    if ((sunrise == 0 && sunset == 0) ||
        (sunrise > 0 && sunset > sunrise && sunset-sunrise < 24*60*60)) {
      s_sunrise=sunrise;
      s_sunset=sunset;
      persist_write_int(PERSIST_KEY_SUNRISE,(int)sunrise);
      persist_write_int(PERSIST_KEY_SUNSET,(int)sunset);
      apply_main_theme();
    }
    return;
  }
  Tuple *font=dict_find(iter, MESSAGE_KEY_FONT_SIZE);
  if (font && font->type == TUPLE_CSTRING) {
    int value=atoi(font->value->cstring);
    if (value >= 0 && value <= 3) {
      s_font_mode=value;
      persist_write_int(PERSIST_KEY_FONT_SIZE, value);
      menu_layer_reload_data(s_menu_layer);
    }
  }
  Tuple *theme=dict_find(iter, MESSAGE_KEY_THEME_MODE);
  if (theme && theme->type == TUPLE_CSTRING) {
    int value=atoi(theme->value->cstring);
    if (value >= 0 && value <= 2) {
      s_theme=value;
      persist_write_int(PERSIST_KEY_THEME, value);
      apply_main_theme();
    }
  }
  Tuple *past=dict_find(iter, MESSAGE_KEY_SHOW_PAST);
  if (past && past->type == TUPLE_CSTRING) {
    s_show_past=atoi(past->value->cstring)!=0;
    persist_write_bool(PERSIST_KEY_SHOW_PAST, s_show_past);
    update_sections();
    menu_layer_reload_data(s_menu_layer);
    restore_selection();
  }
  Tuple *bar=dict_find(iter, MESSAGE_KEY_DETAIL_STATUS_BAR);
  if (bar && bar->type == TUPLE_CSTRING) {
    s_show_status_bar=atoi(bar->value->cstring)!=0;
    persist_write_bool(PERSIST_KEY_STATUS_BAR, s_show_status_bar);
  }
  Tuple *format=dict_find(iter, MESSAGE_KEY_TIME_FORMAT);
  if (format && format->type == TUPLE_CSTRING) {
    const char *val=format->value->cstring;
    s_time_mode=strcmp(val,"24h")==0 ? 2 :
                strcmp(val,"12h")==0 ? 1 : 0;
    persist_write_int(PERSIST_KEY_TIME_MODE, s_time_mode);
    menu_layer_reload_data(s_menu_layer);
    refresh_glance();
  }
  Tuple *total_tuple=dict_find(iter, MESSAGE_KEY_TOTAL_EVENTS);
  Tuple *chunk_tuple=dict_find(iter, MESSAGE_KEY_CHUNK_INDEX);
  Tuple *id_tuple=dict_find(iter, MESSAGE_KEY_SYNC_ID);
  Tuple *checksum_tuple=dict_find(iter, MESSAGE_KEY_SYNC_CHECKSUM);
  Tuple *data_tuple=dict_find(iter, MESSAGE_KEY_EVENT_DATA);
  if (!total_tuple || !chunk_tuple || !id_tuple || !checksum_tuple) return;
  int total=total_tuple->value->int32;
  int chunk=chunk_tuple->value->int32;
  uint32_t id=(uint32_t)id_tuple->value->int32;
  uint32_t expected=(uint32_t)checksum_tuple->value->int32;
  if (!id || total<0 || total>MAX_EVENTS || chunk<0 ||
      (total>0 && chunk >= (total+CHUNK_SIZE-1)/CHUNK_SIZE)) return;
  if (id==s_completed_sync_id) {
    int final_chunk=total ? (total-1)/CHUNK_SIZE : 0;
    if (chunk==final_chunk) send_ack(id, chunk); // Lost final ACK.
    return;
  }
  if (total==0) {
    if (chunk!=0 || expected!=1) return;
    if (s_new_data_buffer) { free(s_new_data_buffer); s_new_data_buffer=NULL; }
    finish_calendar_sync(NULL, 0, id);
    send_ack(id, 0);
    return;
  }
  if (!data_tuple || data_tuple->type!=TUPLE_BYTE_ARRAY) return;
  int count=total-chunk*CHUNK_SIZE;
  if (count > CHUNK_SIZE) count=CHUNK_SIZE;
  if (count<=0 || data_tuple->length != (size_t)(count*sizeof(CalendarEvent))) return;
  if (chunk==0 && (s_active_sync_id!=id || !s_new_data_buffer)) {
    if (s_new_data_buffer) free(s_new_data_buffer);
    s_new_data_buffer=calloc(total, sizeof(CalendarEvent));
    if (!s_new_data_buffer) return;
    s_active_sync_id=id;
    s_events_received_so_far=0;
    s_next_chunk=0;
    s_transfer_checksum=1;
    s_expected_checksum=expected;
    s_sync_in_progress=true;
    text_layer_set_text(s_toast_layer, "Syncing data...");
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
  }
  if (s_active_sync_id != id || !s_new_data_buffer || s_expected_checksum != expected) return;
  if (chunk == s_next_chunk-1) {
    send_ack(id, chunk); // Repeated packet after ACK loss: no double copy/checksum.
    return;
  }
  if (chunk != s_next_chunk) return;
  const CalendarEvent *incoming=(const CalendarEvent*)data_tuple->value->data;
  for (int i=0;i<count;i++) {
    if (!memchr(incoming[i].title, '\0', MAX_TITLE_LEN)) return;
  }
  memcpy(&s_new_data_buffer[chunk*CHUNK_SIZE], data_tuple->value->data, data_tuple->length);
  s_transfer_checksum=checksum_bytes(s_transfer_checksum, data_tuple->value->data, data_tuple->length);
  s_events_received_so_far+=count;
  s_next_chunk++;
  if (s_events_received_so_far == total) {
    if (s_transfer_checksum != s_expected_checksum) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Calendar checksum mismatch; retaining cached events");
      free(s_new_data_buffer);
      s_new_data_buffer=NULL;
      s_active_sync_id=0;
      s_sync_in_progress=false;
      text_layer_set_text(s_toast_layer, "Sync integrity error");
      layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
      return;
    }
    finish_calendar_sync(s_new_data_buffer, total, id);
  }
  send_ack(id, chunk);
}

// --- Main App Lifecycle ---

// Called when the main window is created. Sets up the UI.
static void main_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window); // Get window root layer
  GRect bounds = layer_get_frame(window_layer); // Get screen dimensions
  
  // Create and configure the MenuLayer (The scrolling list)
  s_menu_layer = menu_layer_create(bounds);
  window_set_background_color(window, ui_background());
  menu_layer_set_normal_colors(s_menu_layer, ui_background(), ui_foreground()); // White background, Black text
  menu_layer_set_highlight_colors(s_menu_layer, dark_mode() ? GColorWhite : PBL_IF_COLOR_ELSE(GColorCyan, GColorBlack), dark_mode() ? GColorBlack : PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite)); // Cyan highlight, Black text

  // Register all the drawing/logic callbacks for the MenuLayer
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = get_num_sections_callback,
    .get_num_rows = get_num_rows_callback,
    .get_header_height = get_head_h_callback,
    .get_cell_height = get_cell_h_callback,
    .draw_header = draw_head_callback,
    .draw_row = draw_row_callback,
    .select_click = select_click_callback,
    .select_long_click = select_long_click_callback // <--- NEW: Binds the Long Press callback
  });
  // Allow the MenuLayer to accept button clicks from the window
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  // Add MenuLayer to the window
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  // Create and configure the Notification Toast Layer (Positioned at the very bottom)
  s_toast_layer = text_layer_create(GRect(0, bounds.size.h - 24, bounds.size.w, 24));
  text_layer_set_background_color(s_toast_layer, GColorBlack); // Black background
  text_layer_set_text_color(s_toast_layer, GColorWhite); // White text
  text_layer_set_text_alignment(s_toast_layer, GTextAlignmentCenter); // Center the text
  text_layer_set_font(s_toast_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD)); // Bold font
  text_layer_set_text(s_toast_layer, "Syncing data..."); // Set the text message
  
  // Make the layer invisible by default (It will only show during a sync)
  layer_set_hidden(text_layer_get_layer(s_toast_layer), true); 
  // Add Toast Layer ON TOP of the MenuLayer
  layer_add_child(window_layer, text_layer_get_layer(s_toast_layer));
}

// Called when the main window is destroyed to clean up UI memory
static void main_unload(Window *window) {
  menu_layer_destroy(s_menu_layer); // Destroy the menu layer
  text_layer_destroy(s_toast_layer); // Destroy the toast notification layer
}

// The initialization function that runs immediately when the app opens
static void init() {
  s_main_window = window_create(); // Create main window
  // Assign the load and unload handlers to the window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_load, 
    .unload = main_unload
  });
  
  s_content_size = preferred_content_size();
  if (persist_exists(PERSIST_KEY_FONT_SIZE)) {
    int saved = persist_read_int(PERSIST_KEY_FONT_SIZE);
    if (saved >= 0 && saved <= 3) s_font_mode = saved;
  }
  if (persist_exists(PERSIST_KEY_SELECTION)) s_restore_event_index = persist_read_int(PERSIST_KEY_SELECTION);
  if (persist_exists(PERSIST_KEY_THEME)) s_theme = persist_read_int(PERSIST_KEY_THEME);
  if (persist_exists(PERSIST_KEY_SUNRISE)) s_sunrise=(time_t)persist_read_int(PERSIST_KEY_SUNRISE);
  if (persist_exists(PERSIST_KEY_SUNSET)) s_sunset=(time_t)persist_read_int(PERSIST_KEY_SUNSET);
  if (persist_exists(PERSIST_KEY_SHOW_PAST)) s_show_past = persist_read_bool(PERSIST_KEY_SHOW_PAST);
  if (persist_exists(PERSIST_KEY_STATUS_BAR)) s_show_status_bar = persist_read_bool(PERSIST_KEY_STATUS_BAR);
  if (persist_exists(PERSIST_KEY_TIME_MODE)) s_time_mode = persist_read_int(PERSIST_KEY_TIME_MODE);
  else if (persist_exists(PERSIST_KEY_TIME_24H)) s_time_mode = persist_read_int(PERSIST_KEY_TIME_24H) ? 2 : 1;
  if (persist_exists(PERSIST_KEY_LAST_SYNC)) s_last_sync = (time_t)persist_read_int(PERSIST_KEY_LAST_SYNC);
  // Load user preference for 12/24 hour time
  // Clock preferences are now controlled through s_time_mode.
  
  // Load cached calendar data from permanent storage
  if (persist_exists(PERSIST_KEY_COUNT)) load_data_from_storage();
  
  // Push the main window onto the screen
  window_stack_push(s_main_window, true);
  restore_selection();
  
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  // Register the function to listen for Bluetooth messages
  app_message_register_inbox_received(inbox_handler);
  app_message_register_outbox_failed(ack_outbox_failed);
  // Open the Bluetooth channels (Input buffer: 2048 bytes, Output buffer: 512 bytes)
  app_message_open(512, 128);

  // If the watch has absolutely zero data on launch, force a fetch immediately
  if (s_total_expected_events == 0) {
    s_sync_in_progress = true;
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false); // Show the "Syncing" toast immediately
    request_calendar_sync(false);
  }
}

// The cleanup function that runs right before the app exits completely
static void deinit() {
  tick_timer_service_unsubscribe();
  if (s_ack_timer) app_timer_cancel(s_ack_timer);
  if (s_detail_timeout) { app_timer_cancel(s_detail_timeout); s_detail_timeout=NULL; }
  if (s_pref_timer) { app_timer_cancel(s_pref_timer); s_pref_timer=NULL; }
  if (s_sync_watchdog) app_timer_cancel(s_sync_watchdog);
  if (s_settings_window) window_destroy(s_settings_window);
  if (s_detail_window) window_destroy(s_detail_window);
  window_destroy(s_main_window);
  if (s_event_array) free(s_event_array);
  if (s_new_data_buffer) free(s_new_data_buffer);
}

// The standard main loop required by all C programs
int main(void) { 
  init(); // Run the initialization
  app_event_loop(); // Hand control over to the Pebble OS event loop (waits for button clicks/messages)
  deinit(); // Clean up memory when the user exits the app
}