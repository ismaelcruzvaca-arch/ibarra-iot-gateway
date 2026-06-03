# Apply Progress: Norvi Deep Persistence

- Updated `platformio.ini` to use LittleFS `board_build.filesystem = littlefs`.
- Updated `main.cpp`:
  - Included `<LittleFS.h>`.
  - Added `SpooledEvent` struct definition.
  - Setup `LittleFS.begin(true)` with error handling.
  - Tracked `ntpSynced` for offline spooling timing.
  - Implemented logic in `STATE_DISCONNECTED`/`STATE_CONNECTING_WIFI`/`STATE_SYNCING_TIME`/`STATE_CONNECTING_MQTT` to dequeue events from the live queue, calculate their actual event time, and append them to `/spool.dat` if `ntpSynced` is true.
  - Implemented `STATE_CONNECTED` logic to read up to 10 elements at a time from `/spool.dat` before polling live queue, delaying 50ms between chunks, and purging the file when exhausted.
- Updated `test_main.cpp`:
  - Defined `SpooledEvent` test structural mock.
  - Implemented `test_spooled_event_struct_members` to ensure sizes are correct.
  - Registered test in `setup` and `main`.
