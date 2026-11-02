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
#define MAX_SECTIONS 10
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
static MenuLayer *s_menu_layer;         // The scrolling list layer for the main window
static ScrollLayer *s_detail_scroll_layer;
static TextLayer *s_detail_title_layer; // The text layer showing the event title in the detail window
static TextLayer *s_detail_time_layer;
static TextLayer *s_detail_meta_layer;
static char s_detail_meta[360] = "Loading details...";  // The text layer showing the event time in the detail window
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
static int s_use_24h = 0;                       // Flag: 0 for 12-hour AM/PM format, 1 for 24-hour format
static CalendarSection s_sections[MAX_SECTIONS];// Array holding the grouping data for the Menu headers

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
  if (s_use_24h == 1) { // If the user prefers 24-hour time
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
static void update_sections() {
  if (s_total_expected_events == 0 || !s_event_array) { s_num_sections = 0; return; } // If there is no data, exit the function
  
  s_num_sections = 0; // Reset the section count to zero
  int current_start = 0; // Track the array index where the current day group begins
  time_t now = time(NULL); // Get the current time on the watch
  struct tm tomorrow_tm = *localtime(&now);
  tomorrow_tm.tm_mday += 1;
  tomorrow_tm.tm_isdst = -1;
  time_t tomorrow = mktime(&tomorrow_tm);
  
  for (int i = 0; i < s_total_expected_events; i++) { // Loop through every event
    time_t et = (time_t)s_event_array[i].timestamp; // Get the timestamp for the current event
    
    // If this is the first event, OR if this event is on a different day than the previous event
    if (i == 0 || !is_same_day((time_t)s_event_array[i-1].timestamp, et)) {
      if (s_num_sections > 0) { // If we were already building a section...
        s_sections[s_num_sections - 1].count = i - current_start; // ...close it by calculating how many events it contained
      }
      if (s_num_sections < MAX_SECTIONS) { // Make sure we don't exceed our maximum allowed sections
        s_sections[s_num_sections].start_index = i; // Mark this event as the start of a new section
        
        // Determine the header text: "Today", "Tomorrow", or the full date
        strftime(s_sections[s_num_sections].date_text, 32, 
                 is_same_day(now, et) ? "Today" : 
                 is_same_day(tomorrow, et) ? "Tomorrow" : "%A, %b %d", 
                 localtime(&et));
                 
        current_start = i; // Update the tracking variable to the current index
        s_num_sections++; // Increment the total number of sections
      }
    }
  }
  // After the loop, close the final section by calculating its count
  if (s_num_sections > 0) {
    s_sections[s_num_sections - 1].count = s_total_expected_events - current_start;
  }
}

static int effective_font_mode(void) {
  if (s_font_mode >= 1 && s_font_mode <= 3) return s_font_mode;
  return (s_content_size >= PreferredContentSizeLarge) ? 3 : 2;
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
    if (index >= s_sections[section].start_index &&
        index < s_sections[section].start_index + s_sections[section].count) {
      menu_layer_set_selected_index(s_menu_layer,
          (MenuIndex){.section=section,.row=index-s_sections[section].start_index},
          MenuRowAlignCenter, false);
      return;
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
  if (y < bounds.size.h) y = bounds.size.h;
  scroll_layer_set_content_size(s_detail_scroll_layer, GSize(bounds.size.w, y));
}
static void detail_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  window_set_background_color(window, GColorWhite);
  s_detail_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_detail_scroll_layer, window);
  layer_add_child(root, scroll_layer_get_layer(s_detail_scroll_layer));
  const int font = effective_font_mode();
  s_detail_time_layer = text_layer_create(GRect(0, 0, bounds.size.w, 48));
  text_layer_set_font(s_detail_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(s_detail_time_layer, GColorBlack);
  text_layer_set_background_color(s_detail_time_layer, GColorClear);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_time_layer));
  s_detail_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, 48));
  text_layer_set_font(s_detail_title_layer, fonts_get_system_font(
    font == 3 ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_color(s_detail_title_layer, GColorBlack);
  text_layer_set_background_color(s_detail_title_layer, GColorClear);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_title_layer));
  s_detail_meta_layer = text_layer_create(GRect(0, 0, bounds.size.w, 48));
  text_layer_set_font(s_detail_meta_layer, fonts_get_system_font(
    font == 1 ? FONT_KEY_GOTHIC_18 : (font == 3 ? FONT_KEY_GOTHIC_28 : FONT_KEY_GOTHIC_24)));
  text_layer_set_text_color(s_detail_meta_layer, GColorBlack);
  text_layer_set_background_color(s_detail_meta_layer, GColorClear);
  text_layer_set_text(s_detail_meta_layer, s_detail_meta);
  scroll_layer_add_child(s_detail_scroll_layer, text_layer_get_layer(s_detail_meta_layer));
  if (s_event_array && s_selected_event_index < s_total_expected_events) {
    CalendarEvent *ev = &s_event_array[s_selected_event_index];
    text_layer_set_text(s_detail_title_layer, ev->title);
    static char when[64], begin[16], finish[16], date[20];
    time_t start = (time_t)ev->timestamp;
    strftime(date, sizeof(date), "%a, %b %d", localtime(&start));
    format_time(begin, sizeof(begin), start);
    format_time(finish, sizeof(finish), (time_t)ev->end_timestamp);
    snprintf(when, sizeof(when), "%s\\n%s - %s", date, begin, finish);
    text_layer_set_text(s_detail_time_layer, when);
  }
  layout_detail_content();
}
static void detail_unload(Window *window) {
  text_layer_destroy(s_detail_title_layer);
  text_layer_destroy(s_detail_time_layer);
  text_layer_destroy(s_detail_meta_layer);
  s_detail_meta_layer = NULL;
  scroll_layer_destroy(s_detail_scroll_layer);
}

// Opens the detail window for a specific event index
static void show_detail(int index) {
  if (!s_event_array || index < 0 || index >= s_total_expected_events) return;
  snprintf(s_detail_meta, sizeof(s_detail_meta), "Loading details...");
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
    app_message_outbox_send();
  }
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
  CalendarEvent ev = s_event_array[s_sections[i->section].start_index + i->row];
  graphics_context_set_fill_color(ctx, event_color(ev.color));
  graphics_fill_rect(ctx, GRect(0, 3, 4, row_height() - 6), 0, GCornerNone);
  static char t_b[16]; 
  format_time(t_b, 16, ev.timestamp); // Format the start time
  
  MenuIndex selected = menu_layer_get_selected_index(s_menu_layer);
  bool highlighted = selected.section == i->section && selected.row == i->row;
  graphics_context_set_text_color(ctx, highlighted ? PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite) : GColorBlack); // Keep highlighted text readable
  // Draw the time text in the top half of the row
  graphics_draw_text(ctx, t_b, fonts_get_system_font(time_font()), GRect(9, 0, layer_get_bounds(l).size.w - 14, 20), 0, GTextAlignmentLeft, NULL);
  // Respect the user's system text size.
  graphics_draw_text(ctx, ev.title, fonts_get_system_font(event_font()), GRect(9, 16, layer_get_bounds(l).size.w - 14, row_height() - 17), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// Handles what happens when the user clicks the "Select" (middle) button on a row
static void select_click_callback(MenuLayer *m, MenuIndex *i, void *d) { 
  if(s_num_sections > 0) { // If actual data exists
    s_restore_event_index = s_sections[i->section].start_index + i->row;
    persist_write_int(PERSIST_KEY_SELECTION, s_restore_event_index);
    show_detail(s_sections[i->section].start_index + i->row); // Open the detail window for that specific event
  }
}

// Handles what happens when the user LONG-PRESSES the "Select" (middle) button on a row
static void select_long_click_callback(MenuLayer *m, MenuIndex *i, void *d) {
  // 1. Immediately unhide the "Syncing..." toast so the user knows it worked
  layer_set_hidden(text_layer_get_layer(s_toast_layer), false); 
  
  // 2. Send the "Help Signal" to the phone. 
  // (The JS is programmed to ignore the 30-min timer whenever it receives this)
  DictionaryIterator *out;
  app_message_outbox_begin(&out); // Start an outgoing message
  dict_write_uint8(out, 0, 1); // Write a dummy byte just to trigger the listener
  app_message_outbox_send(); // Send the message to the phone
}

// --- Communications ---

// Handles all incoming messages from the phone (JavaScript)
static void inbox_handler(DictionaryIterator *iter, void *ctx) {
  
  Tuple *detail = dict_find(iter, MESSAGE_KEY_DETAIL_TEXT);
  if (detail && detail->type == TUPLE_CSTRING) {
    snprintf(s_detail_meta, sizeof(s_detail_meta), "%s", detail->value->cstring);
    if (s_detail_meta_layer) {
      text_layer_set_text(s_detail_meta_layer, s_detail_meta);
      layout_detail_content();
    }
    return;
  }
  Tuple *status = dict_find(iter, MESSAGE_KEY_SYNC_STATUS);
  if (status && status->type == TUPLE_CSTRING && strcmp(status->value->cstring, "error") == 0) {
    s_sync_in_progress = false;
    text_layer_set_text(s_toast_layer, "Sync failed");
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
    menu_layer_reload_data(s_menu_layer);
    return;
  }
  Tuple *font_tuple = dict_find(iter, MESSAGE_KEY_FONT_SIZE);
  if (font_tuple && font_tuple->type == TUPLE_CSTRING) {
    int mode = atoi(font_tuple->value->cstring);
    if (mode >= 0 && mode <= 3) {
      s_font_mode = mode;
      persist_write_int(PERSIST_KEY_FONT_SIZE, mode);
      menu_layer_reload_data(s_menu_layer);
    }
  }
  // 1. Check if the message contains a 12h/24h time format setting update
  Tuple *f_t = dict_find(iter, MESSAGE_KEY_TIME_FORMAT);
  if (f_t) { 
    s_use_24h = (strcmp(f_t->value->cstring, "24h") == 0); // Convert the string "24h" to an integer 1
    persist_write_int(PERSIST_KEY_TIME_24H, s_use_24h); // Save preference permanently
    menu_layer_reload_data(s_menu_layer); // Refresh the menu to apply new time format
  }

  // 2. Check if the message contains Calendar Data
  Tuple *t_t = dict_find(iter, MESSAGE_KEY_TOTAL_EVENTS); // Total expected events
  Tuple *c_t = dict_find(iter, MESSAGE_KEY_CHUNK_INDEX);  // Which chunk this is
  Tuple *d_t = dict_find(iter, MESSAGE_KEY_EVENT_DATA);   // The actual byte array of data

  // Strictly validate incoming chunks before writing into watch memory.
  if (t_t && c_t && d_t) {
    int total = t_t->value->int32;
    int chunk = c_t->value->int32;
    if (total < 0 || total > MAX_EVENTS || chunk < 0 ||
        (total > 0 && chunk >= (total + CHUNK_SIZE - 1) / CHUNK_SIZE) ||
        d_t->type != TUPLE_BYTE_ARRAY) return;
    int expected_count = total - chunk * CHUNK_SIZE;
    if (expected_count > CHUNK_SIZE) expected_count = CHUNK_SIZE;
    if (total == 0) {
      if (chunk != 0 || d_t->length != 0) return;
      if (s_new_data_buffer) free(s_new_data_buffer);
      s_new_data_buffer = NULL;
      if (s_event_array) free(s_event_array);
      s_event_array = NULL;
      s_total_expected_events = 0;
      s_num_sections = 0;
      s_sync_in_progress = false;
      persist_write_int(PERSIST_KEY_VERSION, STORAGE_VERSION);
      persist_write_int(PERSIST_KEY_COUNT, 0);
      menu_layer_reload_data(s_menu_layer);
      refresh_glance();
      layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
      return;
    }
    if (expected_count <= 0 ||
        d_t->length != expected_count * sizeof(CalendarEvent)) return;
    if (chunk == 0) {
      if (s_new_data_buffer) free(s_new_data_buffer);
      s_new_data_buffer = calloc(total, sizeof(CalendarEvent));
      s_events_received_so_far = 0;
      s_next_chunk = 0;
      s_sync_in_progress = (s_new_data_buffer != NULL);
      text_layer_set_text(s_toast_layer, "Syncing data...");
      layer_set_hidden(text_layer_get_layer(s_toast_layer), !s_sync_in_progress);
    }
    if (!s_sync_in_progress || chunk != s_next_chunk) return;
    const CalendarEvent *incoming = (const CalendarEvent *)d_t->value->data;
    for (int i = 0; i < expected_count; i++) {
      if (!memchr(incoming[i].title, 0, MAX_TITLE_LEN)) return;
    }
    memcpy(&s_new_data_buffer[chunk * CHUNK_SIZE],
           d_t->value->data, d_t->length);
    s_events_received_so_far += expected_count;
    s_next_chunk++;
    if (s_events_received_so_far == total) {
      if (s_event_array) free(s_event_array);
      s_event_array = s_new_data_buffer;
      s_new_data_buffer = NULL;
      s_total_expected_events = total;
      s_sync_in_progress = false;
      update_sections();
      menu_layer_reload_data(s_menu_layer);
      restore_selection();
      save_data_to_storage();
      refresh_glance();
      layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
    }
  }
}

// --- Main App Lifecycle ---

// Called when the main window is created. Sets up the UI.
static void main_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window); // Get window root layer
  GRect bounds = layer_get_frame(window_layer); // Get screen dimensions
  
  // Create and configure the MenuLayer (The scrolling list)
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack); // White background, Black text
  menu_layer_set_highlight_colors(s_menu_layer, PBL_IF_COLOR_ELSE(GColorCyan, GColorBlack), PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite)); // Cyan highlight, Black text

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
  // Load user preference for 12/24 hour time
  if (persist_exists(PERSIST_KEY_TIME_24H)) s_use_24h = persist_read_int(PERSIST_KEY_TIME_24H);
  
  // Load cached calendar data from permanent storage
  if (persist_exists(PERSIST_KEY_COUNT)) load_data_from_storage();
  
  // Push the main window onto the screen
  window_stack_push(s_main_window, true);
  restore_selection();
  
  // Register the function to listen for Bluetooth messages
  app_message_register_inbox_received(inbox_handler);
  // Open the Bluetooth channels (Input buffer: 2048 bytes, Output buffer: 512 bytes)
  app_message_open(512, 128);

  // If the watch has absolutely zero data on launch, force a fetch immediately
  if (s_total_expected_events == 0) {
    s_sync_in_progress = true;
    layer_set_hidden(text_layer_get_layer(s_toast_layer), false); // Show the "Syncing" toast immediately
    DictionaryIterator *out;
    app_message_outbox_begin(&out); // Start an outgoing message
    dict_write_uint8(out, 0, 1); // Write a dummy byte to act as a signal ping
    app_message_outbox_send(); // Send the message to the phone
  }
}

// The cleanup function that runs right before the app exits completely
static void deinit() {
  window_destroy(s_main_window); // Destroy the main window
  if (s_detail_window) window_destroy(s_detail_window); // Destroy detail window if it was left open
  if (s_event_array) { free(s_event_array); s_event_array = NULL; } // Free active data memory
  if (s_new_data_buffer) { free(s_new_data_buffer); s_new_data_buffer = NULL; } // Free incoming buffer memory
}

// The standard main loop required by all C programs
int main(void) { 
  init(); // Run the initialization
  app_event_loop(); // Hand control over to the Pebble OS event loop (waits for button clicks/messages)
  deinit(); // Clean up memory when the user exits the app
}