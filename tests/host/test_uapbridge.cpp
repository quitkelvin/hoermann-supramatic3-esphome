// Host test of the command/state logic of uapbridge_esp with simulated bus frames (no hardware).
// Build + run: tests/host/run.sh
#include "esphome/components/uapbridge_esp/uapbridge_esp.h"

uint32_t esphome::fake_millis = 1000;
std::deque<uint8_t> esphome::uart::fake_rx;
std::vector<std::vector<uint8_t>> esphome::uart::fake_tx;
bool g_log_enabled = false;

using namespace esphome;
using U = uapbridge_esp::UAPBridge_esp;

static int failures = 0, checks = 0;
#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, what); } } while (0)

// test access to protected members
struct T : public U {
  using U::next_action; using U::pending_action; using U::valid_broadcast; using U::state;
  using U::crc_errors; using U::valid_broadcasts; using U::inject_fail_streak; using U::passive;
  uint8_t cnt = 0;
  void push(std::vector<uint8_t> f) { for (auto b : f) uart::fake_rx.push_back(b); }
  void broadcast(uint16_t status, bool corrupt = false) {
    uint8_t f[5] = {0x00, (uint8_t) ((cnt++ << 4) | 0x02), (uint8_t) (status & 0xFF), (uint8_t) (status >> 8), 0};
    f[4] = calc_crc8(f, 4);
    if (corrupt) f[4] ^= 0x5A;
    push({0x00 /* sync break */, f[0], f[1], f[2], f[3], f[4]});
  }
  void status_request() {
    uint8_t f[4] = {0x28, (uint8_t) ((cnt++ << 4) | 0x01), 0x20, 0};
    f[3] = calc_crc8(f, 3);
    push({0x00, f[0], f[1], f[2], f[3]});
  }
};

struct Sim {
  T d;
  text_sensor::TextSensor derived, result, last;
  sensor::Sensor secs, streak, valid, crc;
  uint16_t status = 0x0002;
  bool drive_on = true;     // drive sends broadcasts
  bool polls = true;        // drive sends status requests to the UAP1 address
  std::vector<uint16_t> sent;   // commands the board put on the bus
  Sim(bool p_mode = true) {
    uart::fake_rx.clear(); uart::fake_tx.clear();
    d.set_inject_mode(true); d.set_einwurf_v2(true); d.set_p_modus(p_mode);
    d.set_status_abgeleitet_sensor(&derived); d.set_command_result_sensor(&result); d.set_letzter_befehl_sensor(&last);
    d.set_seconds_since_broadcast_sensor(&secs); d.set_inject_failures_sensor(&streak);
    d.set_valid_broadcasts_sensor(&valid); d.set_crc_errors_sensor(&crc);
  }
  // run ms of bus time: broadcast every 100 ms, status request every 100 ms (offset 50 ms), loop every 5 ms
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) {
      fake_millis += 5;
      if (drive_on && fake_millis % 100 == 0) d.broadcast(status);
      if (drive_on && polls && fake_millis % 100 == 50) d.status_request();
      for (int i = 0; i < 8; i++) d.loop();
      for (auto &f : uart::fake_tx) if (f.size() == 6 && f[2] == 0x29) sent.push_back(f[3] | (f[4] << 8));
      uart::fake_tx.clear();
    }
  }
  int count(uint16_t cmd) { int n = 0; for (auto c : sent) n += (c == cmd); return n; }
};

static const uint16_t OPEN = 0x1001, CLOSE = 0x1002, STOP = 0x10FF, LIGHT = 0x1008;

int main() {
  {
    puts("T1 no connection -> commands rejected, state unknown");
    Sim s; s.drive_on = false; s.run(500);
    CHECK(!s.d.valid_broadcast, "not connected without broadcast");
    s.d.action_open();
    CHECK(s.d.next_action == U::hoermann_action_none, "open rejected while not connected");
    s.run(200);
    CHECK(s.result.state == "rejected", "result rejected");
    CHECK(s.derived.state == "unknown", "derived unknown");
  }
  {
    puts("T2 connect, closed; T3 Stop replaces a waiting Open, Open never sent");
    Sim s; s.polls = false; s.run(500);
    CHECK(s.d.valid_broadcast, "connected");
    CHECK(s.d.state == U::hoermann_state_closed, "state closed");
    CHECK(s.derived.state == "Closed", "derived Closed");
    s.d.action_open();
    CHECK(s.d.next_action == U::hoermann_action_open, "open waiting");
    s.d.action_stop();
    CHECK(s.d.next_action == U::hoermann_action_stop, "stop replaced the waiting open");
    s.d.action_open();
    CHECK(s.d.next_action == U::hoermann_action_stop, "travel command does not replace a waiting stop");
    s.polls = true; s.run(10000);
    CHECK(s.count(STOP) == 1, "stop sent once");
    CHECK(s.count(OPEN) == 0, "open never sent");
  }
  {
    puts("T4 Open sent + waiting for retry, then Stop: no further Open ever");
    Sim s; s.run(500);
    s.d.action_open(); s.run(300);
    CHECK(s.count(OPEN) == 1, "open sent");
    s.polls = false; s.run(2500);   // no effect -> retry queued (next_action = open)
    CHECK(s.d.next_action == U::hoermann_action_open, "retry queued");
    s.d.action_stop();
    CHECK(s.d.next_action == U::hoermann_action_stop && s.d.pending_action == U::hoermann_action_none, "stop replaced retry");
    s.polls = true; s.run(10000);
    CHECK(s.count(STOP) == 1, "stop sent");
    CHECK(s.count(OPEN) == 1, "open not repeated after stop");
  }
  {
    puts("T5 stop_priority: false -> Stop rejected while Open waits (1.0.0 behaviour)");
    Sim s; s.d.set_stop_priority(false); s.polls = false; s.run(500);
    s.d.action_open(); s.d.action_stop();
    CHECK(s.d.next_action == U::hoermann_action_open, "stop rejected");
  }
  {
    puts("T6 own Open confirmed -> Opening; result confirmed; Open reached");
    Sim s; s.run(500);
    s.d.action_open(); s.run(300);
    s.status = 0x0000; s.run(1000);
    CHECK(s.derived.state == "Opening", "derived Opening");
    CHECK(s.result.state == "confirmed", "result confirmed");
    s.status = 0x0001; s.run(500);
    CHECK(s.derived.state == "Open", "derived Open");
  }
  {
    puts("T7 travel by remote (no own command) -> Moving (direction unknown), later estimate");
    Sim s; s.run(500);
    s.status = 0x0000; s.run(1000);
    CHECK(s.derived.state == "Moving (direction unknown)", "direction unknown");
    s.run(26000);
    CHECK(s.derived.state == "Stopped between end positions (venting assumed)", "estimate after 25 s");
    s.status = 0x0001; s.run(500);
    CHECK(s.derived.state == "Open", "open");
  }
  {
    puts("T8 old own command does not set the direction of a later remote travel");
    Sim s; s.status = 0x0001; s.run(500);
    s.d.action_close(); s.run(300);
    s.run(12000);                 // door did not react (3 attempts, last one > 5 s ago) ...
    s.status = 0x0000; s.run(500); // ... then someone uses the remote
    CHECK(s.derived.state == "Moving (direction unknown)", "stale command ignored");
  }
  {
    puts("T9 connection timeout -> unknown, waiting command dropped, automatic recovery");
    Sim s; s.d.set_connection_timeout(2000); s.status = 0x0001; s.run(500);
    s.polls = false; s.d.action_close();
    s.drive_on = false; s.run(2500);
    CHECK(!s.d.valid_broadcast, "not connected after timeout");
    CHECK(s.d.state == U::hoermann_state_unkown, "state unknown");
    CHECK(s.d.next_action == U::hoermann_action_none, "waiting command dropped");
    CHECK(s.derived.state == "unknown", "derived unknown");
    CHECK(s.secs.state >= 2, "seconds since broadcast");
    s.d.action_open();
    CHECK(s.d.next_action == U::hoermann_action_none, "rejected while disconnected");
    s.drive_on = true; s.polls = true; s.run(1500);
    CHECK(s.d.valid_broadcast && s.d.state == U::hoermann_state_open, "recovered");
    CHECK(s.count(CLOSE) == 0, "dropped close never sent");
    CHECK(s.secs.state == 0, "seconds since broadcast back to 0");
  }
  {
    puts("T10 5 failed injects in a row -> 60 s passive");
    Sim s; s.polls = false; s.run(500);
    for (int i = 0; i < 5; i++) { s.d.action_open(); s.run(3500); }
    CHECK(s.d.inject_fail_streak == 5 && s.streak.state == 5, "streak 5");
    CHECK(s.d.passive, "passive");
    s.d.action_open();
    CHECK(s.d.next_action == U::hoermann_action_none, "rejected while passive");
    s.run(61000);
    s.d.action_open();
    CHECK(s.d.next_action == U::hoermann_action_open, "accepted after pause");
    s.polls = true; s.run(300); s.status = 0x0000; s.run(500);
    CHECK(s.d.inject_fail_streak == 0, "streak reset after confirmed command");
  }
  {
    puts("T11 frame counters: valid broadcasts and CRC errors");
    Sim s; s.run(1000);
    uint32_t v = s.d.valid_broadcasts;
    CHECK(v >= 9, "valid broadcasts counted");
    s.d.broadcast(0x0002, true); s.run(5);
    CHECK(s.d.crc_errors >= 1, "crc error counted");
  }
  {
    puts("T12 P series light: toggle only, missing confirmation is 'not observable', no retry, no failure");
    Sim s; s.run(500);
    CHECK(!s.d.is_light_state_reported(), "light state not reported in P mode");
    s.d.set_light(true); s.run(4000);
    s.d.set_light(false); s.run(4000);
    CHECK(s.count(LIGHT) == 2, "two toggles, no repeats");
    CHECK(s.result.state == "not observable", "not observable");
    CHECK(s.d.inject_fail_streak == 0, "not a failure");
  }
  {
    puts("T13 E series: light bit reported, ON when already on sends nothing");
    Sim s(false); s.status = 0x0002 | 0x0008; s.run(500);
    CHECK(s.d.is_light_state_reported(), "reported");
    s.d.set_light(true); s.run(1000);
    CHECK(s.count(LIGHT) == 0, "no toggle");
  }
  {
    puts("T14 monitor mode: never sends");
    Sim s; s.d.set_monitor_mode(true); s.run(500);
    s.d.action_open(); s.d.action_toggle_light(); s.d.action_stop(); s.run(3000);
    CHECK(s.sent.empty(), "nothing sent");
    CHECK(s.result.state == "rejected", "rejected");
  }
  {
    puts("T15 connection_timeout: 0 = off");
    Sim s; s.d.set_connection_timeout(0); s.run(500);
    s.drive_on = false; s.run(10000);
    CHECK(s.d.valid_broadcast, "stays connected with timeout off");
  }
  printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
