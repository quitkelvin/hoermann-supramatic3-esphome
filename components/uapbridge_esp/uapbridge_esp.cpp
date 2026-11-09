#include "uapbridge_esp.h"

namespace esphome {
namespace uapbridge_esp {
static const char *const TAG = "uapbridge_esp";

void UAPBridge_esp::loop() {
  this->loop_fast();
  if (this->einwurf_v2) {
    this->inject_check_v2();
    // v2: while a command is pending/being verified, run the main loop without the 16 ms pause
    // so the answer to the status request is on the bus before the real UAP1 answers.
    bool need = (this->next_action != hoermann_action_none) || (this->pending_action != hoermann_action_none);
    if (need && !this->hf_active) { this->high_freq_.start(); this->hf_active = true; }
    else if (!need && this->hf_active) { this->high_freq_.stop(); this->hf_active = false; }
  } else {
    this->inject_check();
  }
  this->loop_slow();

  if (this->data_has_changed) {
    ESP_LOGD(TAG, "UAPBridge_esp::loop() - received Data has changed.");
    this->clear_data_changed_flag();
    this->state_callback_.call();
  }
}

void UAPBridge_esp::inject_check() {
  if (!this->inject_mode || this->pending_action == hoermann_action_none) return;
  if (millis() - this->pending_time < 600) return;
  uint16_t s = this->broadcast_status, s0 = this->pending_status0;
  bool ok = false;
  switch (this->pending_action) {
    case hoermann_action_toggle_light: ok = ((s ^ s0) & hoermann_state_light_relay) != 0; break;
    case hoermann_action_venting: ok = (s & (hoermann_state_moving | hoermann_state_venting)) != 0; break;
    case hoermann_action_open:  ok = (s & hoermann_state_open) || ((s & (hoermann_state_direction | hoermann_state_moving)) == hoermann_state_opening); break;
    case hoermann_action_close: ok = (s & hoermann_state_closed) || ((s & (hoermann_state_direction | hoermann_state_moving)) == hoermann_state_closing); break;
    case hoermann_action_stop:  ok = !(s & hoermann_state_moving); break;
    default: ok = true; break;
  }
  if (ok) {
    ESP_LOGI(TAG, "Inject: command %04X effective after %d attempt(s)", this->pending_action, this->pending_tries);
    this->pending_action = hoermann_action_none; this->pending_tries = 0;
  } else if (this->pending_tries >= 3) {
    ESP_LOGW(TAG, "Inject: command %04X without effect after 3 attempts - giving up", this->pending_action);
    this->pending_action = hoermann_action_none; this->pending_tries = 0;
  } else {
    this->next_action = this->pending_action;   // next attempt at the next status request
    this->pending_time = millis();
  }
}

// ---------------- inject mode v2 ----------------
const char *UAPBridge_esp::action_name(uint16_t a) {
  switch (a) {
    case hoermann_action_open: return "Open";
    case hoermann_action_close: return "Close";
    case hoermann_action_stop: return "Stop";
    case hoermann_action_toggle_light: return "Light";
    case hoermann_action_venting: return "Venting";
    case hoermann_action_impulse: return "Impulse";
    default: return "?";
  }
}

void UAPBridge_esp::v2_finish(const char *ergebnis) {
  snprintf(this->msg_buf, sizeof(this->msg_buf), "%04X %s: %s after %d attempt%s (0x%04X->0x%04X), fetched after %u ms",
           this->pending_action, action_name(this->pending_action), ergebnis, this->pending_tries,
           this->pending_tries == 1 ? "" : "s", this->pending_status0, this->broadcast_status, (unsigned) this->fetch_ms);
  this->msg_dirty = true;
  this->pending_action = hoermann_action_none;
  this->pending_tries = 0;
}

void UAPBridge_esp::inject_check_v2() {
  if (!this->inject_mode) return;
  uint32_t now = millis();
  // A command is only fetched with a status request of the drive to 0x28. If none comes, do not wait forever
  // (before: next_action stayed set -> every further command was rejected, pending never reset).
  if (this->next_action != hoermann_action_none) {
    if (now - this->next_set_time > 3000) {
      snprintf(this->msg_buf, sizeof(this->msg_buf), "%04X %s: not fetched (no status request to UAP1 within 3 s), attempt %d",
               this->next_action, action_name(this->next_action), this->pending_tries + 1);
      this->msg_dirty = true;
      this->next_action = hoermann_action_none;
      this->pending_action = hoermann_action_none;
      this->pending_tries = 0;
    }
    return;
  }
  if (this->pending_action == hoermann_action_none) return;

  const uint16_t s = this->broadcast_status, s0 = this->pending_status0;
  const uint16_t mv = hoermann_state_direction | hoermann_state_moving;
  bool ok = false;
  uint32_t window;
  switch (this->pending_action) {
    case hoermann_action_toggle_light:
      ok = ((s ^ s0) & hoermann_state_light_relay) != 0;
      window = 2500;
      break;
    case hoermann_action_venting:
      if (this->p_modus) {
        // P series: venting = end position left + standstill without end position (status stays 0x0000)
        const uint16_t e0 = s0 & (hoermann_state_open | hoermann_state_closed);
        const uint16_t e = s & (hoermann_state_open | hoermann_state_closed);
        if (!this->vent_left) {
          if (e0 != 0 && !(s & e0)) { this->vent_left = true; this->vent_left_time = now; return; }
          window = this->retryable ? 2000 : 30000;
          break;   // ok = false -> retry/unconfirmed after window
        }
        if (e & hoermann_state_open) { this->v2_finish("reached Open instead of venting"); return; }
        if (e & hoermann_state_closed) { this->v2_finish("back in Closed instead of venting"); return; }
        if (now - this->vent_left_time >= 25000) { this->v2_finish("confirmed (end position left, 25 s without end position)"); return; }
        return;
      }
      ok = (s & (hoermann_state_moving | hoermann_state_venting)) ||
           ((s0 & hoermann_state_closed) && !(s & hoermann_state_closed)) ||
           ((s0 & hoermann_state_open) && !(s & hoermann_state_open));
      window = this->retryable ? 2000 : 30000;
      break;
    case hoermann_action_open:
      ok = (s & hoermann_state_open) || ((s & mv) == hoermann_state_opening) ||
           ((s0 & hoermann_state_closed) && !(s & hoermann_state_closed));
      window = this->retryable ? 2000 : 45000;
      break;
    case hoermann_action_close:
      ok = (s & hoermann_state_closed) || ((s & mv) == hoermann_state_closing) ||
           ((s0 & hoermann_state_open) && !(s & hoermann_state_open));
      window = this->retryable ? 2000 : 45000;
      break;
    case hoermann_action_stop:
      ok = (s0 & hoermann_state_moving) && !(s & hoermann_state_moving);  // only measurable if the drive reports moving bits
      window = 2000;
      break;
    default:
      ok = false;
      window = 2000;
      break;
  }
  if (ok) { this->v2_finish("confirmed"); return; }
  if (now - this->pending_time < window) return;
  if (this->retryable && this->pending_tries < 3) {
    this->next_action = this->pending_action;   // effect would have been measurable -> next attempt at next request
    this->next_set_time = now;
    return;
  }
  this->v2_finish(this->retryable ? "without effect" : "sent, unconfirmed");
}

void UAPBridge_esp::update_derived() {
  if (this->status_abgeleitet_sensor_ == nullptr) return;
  const char *d;
  const uint32_t now = millis();
  if (this->pos_prev == 0xFFFF) d = "unknown";
  else if (this->pos_prev & hoermann_state_open) d = "Open";
  else if (this->pos_prev & hoermann_state_closed) d = "Closed";
  else {
    uint32_t ref = this->zero_since;
    if (this->last_move_cmd != hoermann_action_none && (int32_t)(this->last_move_time - ref) > 0) ref = this->last_move_time;
    if (this->last_move_cmd == hoermann_action_stop) d = "Stopped";
    else if (now - ref > 25000) d = (this->last_move_cmd == hoermann_action_venting) ? "Venting" : "Stopped between end positions (venting?)";
    else if (this->last_move_cmd == hoermann_action_open) d = "Opening";
    else if (this->last_move_cmd == hoermann_action_close) d = "Closing";
    else if (this->last_move_cmd == hoermann_action_venting) d = "Moving to venting";
    else if (this->last_endlage == 2) d = "Opening";
    else if (this->last_endlage == 1) d = "Closing";
    else d = "Intermediate position";
  }
  if (d != this->derived_prev) {   // pointers to literals -> comparison is enough
    this->derived_prev = d;
    this->status_abgeleitet_sensor_->publish_state(d);
  }
}

void UAPBridge_esp::publish_diag() {
  if (this->next_action != hoermann_action_none) return;   // do not publish while waiting for the status request
  this->update_derived();
  if (this->msg_dirty) {
    this->msg_dirty = false;
    if (this->einwurf_v2) ESP_LOGI(TAG, "Inject: %s", this->msg_buf);
    if (this->letzter_befehl_sensor_ != nullptr) this->letzter_befehl_sensor_->publish_state(this->msg_buf);
  }
  if (this->raw_dirty && this->rohstatus_sensor_ != nullptr) {
    this->raw_dirty = false;
    char b[8];
    snprintf(b, sizeof(b), "0x%04X", this->last_raw);
    this->rohstatus_sensor_->publish_state(b);
  }
}
// ---------------- /inject mode v2 ----------------

void UAPBridge_esp::loop_fast() {
  this->receive();
  if (this->einwurf_v2 && this->send_time != 0) {   // v2: answer immediately, not only after the 1 ms cycle
    this->transmit();
    this->send_time = 0;
  }

  if (millis() - this->last_call < CYCLE_TIME) {
    // avoid unnecessary frequent calls 
    return;
  }
  this->last_call = millis();
  if(this->send_time!=0 && (millis() >= this->send_time)) {
    ESP_LOGVV(TAG, "loop: transmitting");
    this->transmit();
    this->send_time = 0;
  }
}
/**
 * this seems to be a function that only logs stati and makes errorcorrections
 */
void UAPBridge_esp::loop_slow() {
  if (millis() - this->last_call_slow < CYCLE_TIME_SLOW) {
    // avoid unnecessary frequent calls 
    return;
  }
  this->last_call_slow = millis();
  this->publish_diag();   // diagnostic sensors only here (not in the receive path)

    if (this->ignore_next_event) {
      this->ignore_next_event = false;
    } else {
      ESP_LOGD(TAG, "loop_slow called - %02x %02x == %d", (uint8_t)(this->broadcast_status >> 8), (uint8_t)this->broadcast_status, this->broadcast_status);
      hoermann_state_t new_state = hoermann_state_stopped;

      if (this->broadcast_status & hoermann_state_open) {
        new_state = hoermann_state_open;
      } else if (this->broadcast_status & hoermann_state_closed) {
        new_state = hoermann_state_closed;
      } else if ((this->broadcast_status & (hoermann_state_direction | hoermann_state_moving)) == hoermann_state_opening) {
        new_state = hoermann_state_opening;
      } else if ((this->broadcast_status & (hoermann_state_direction | hoermann_state_moving)) == hoermann_state_closing) {
        new_state = hoermann_state_closing;
      } else if (this->broadcast_status & hoermann_state_venting) {
        new_state = hoermann_state_venting;
      }

      if (new_state != this->state) {
        this->handle_state_change(new_state);
      }

      this->update_boolean_state("relay", this->relay_enabled, (this->broadcast_status & hoermann_state_opt_relay));
      this->update_boolean_state("light", this->light_enabled, (this->broadcast_status & hoermann_state_light_relay));
      this->update_boolean_state("vent", this->venting_enabled, (this->broadcast_status & hoermann_state_venting));
      this->update_boolean_state("err", this->error_state, (this->broadcast_status & hoermann_state_error));
      this->update_boolean_state("prewarn", this->prewarn_state, (this->broadcast_status & hoermann_state_prewarn));

      // --- Auto Error Correction ---
      if(this->auto_correction) {
        // if error just came up
        if (((this->broadcast_status & hoermann_state_error) == hoermann_state_error)) {
          // if an error is detected and door is open/closed then try to reset it by requesting opening/closing without movement
          ESP_LOGD(TAG, "autocorrection started");
          if (new_state == hoermann_state_open) {
            this->set_command(true, hoermann_action_open);
          } else if (new_state == hoermann_state_closed) {
            this->set_command(true, hoermann_action_close);
          } else if (new_state == hoermann_state_stopped) {
            // in this state it is not possible to clear the error. But the next open or close cycle will clear it
            this->auto_correction_in_progress = false;
          }
          this->auto_correction_in_progress = true;
        }
        // HINT: i guess if light is on it is sufficent to toggle the light off to reset the error
        // HINT: propably not. this will disable the lamp after correcting the error
        // or both
        if (this->auto_correction_in_progress && (this->broadcast_status & hoermann_state_light_relay)) {
          this->set_command(true, hoermann_action_toggle_light);
          this->auto_correction_in_progress = false;
        }
      }
      // --- Auto Error Correction ---
    }
}

void UAPBridge_esp::receive() {
  bool   newData = false;
  while (this->available() > 0) {
#if ESPHOME_LOGLEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
    if (this->byte_cnt > 5) {
    	// data have not been fetched and will be ignored --> log them at least for debugging purposes
    	char temp[4];
    	sprintf(temp, "%02X ", this->rx_data[0]);
    	ESP_LOGVV(TAG, "in receive while avaiable: %s", temp);
    }
#endif
    // shift old elements and read new; only the last 5 bytes are evaluated; if there are more in the buffer, the older ones are ignored
    for (uint8_t i = 0; i < 4; i++) {
      this->rx_data[i] = this->rx_data[i+1];
    }
    if(this->read_byte(&this->rx_data[4])){
      //if read was successful
      this->byte_cnt++;
    }
    newData = true;
    if (this->einwurf_v2) {
      // v2: evaluate every byte (before: only the last 5 bytes of a burst -> requests were lost with a slow loop)
      this->parse_frame();
      if (this->send_time != 0) break;   // drive waits for an answer -> send now, read the rest later
    }
  }
  if (newData && !this->einwurf_v2) {
    this->parse_frame();
  }
}

void UAPBridge_esp::parse_frame() {
  uint8_t   length  = 0;
  uint8_t   counter = 0;
  {
    ESP_LOGVV(TAG, "new data received");
    // Slave scan
    // 28 82 01 80 06
    if (this->rx_data[0] == UAP1_ADDR) {
      length = this->rx_data[1] & 0x0F;
      if (!this->inject_mode && this->rx_data[2] == CMD_SLAVE_SCAN && this->rx_data[3] == UAP1_ADDR_MASTER && length == 2 && calc_crc8(this->rx_data, length + 3) == 0x00) {
        ESP_LOGVV(TAG, "SlaveScan: %s", print_data(this->rx_data, 0, 5));
        ESP_LOGV(TAG, "->      SlaveScan"); 
        counter = (this->rx_data[1] & 0xF0) + 0x10;
        this->tx_data[0] = UAP1_ADDR_MASTER;
        this->tx_data[1] = 0x02 | counter;
        this->tx_data[2] = UAP1_TYPE;
        this->tx_data[3] = UAP1_ADDR;
        this->tx_data[4] = calc_crc8(this->tx_data, 4);
        this->tx_length = 5;
        this->send_time = millis();
      }
    }
    // Broadcast status
    // 00 92 12 02 35
    if (this->rx_data[0] == BROADCAST_ADDR) {
      length = this->rx_data[1] & 0x0F;
      if (length == 2 && calc_crc8(this->rx_data, length + 3) == 0x00) {
        ESP_LOGVV(TAG, "Broadcast: %s", print_data(this->rx_data, 0, 5));
        ESP_LOGV(TAG, "->      Broadcast");
        this->broadcast_status = this->rx_data[2];
        this->broadcast_status |= (uint16_t)this->rx_data[3] << 8;
        if (this->broadcast_status != this->last_raw) { this->last_raw = this->broadcast_status; this->raw_dirty = true; }
        if (this->broadcast_status & (hoermann_state_moving | hoermann_state_venting)) this->motion_bits_seen = true;
        {
          uint16_t pos = this->broadcast_status & (hoermann_state_open | hoermann_state_closed);
          if (pos != this->pos_prev) {
            if (pos & hoermann_state_open) { this->last_endlage = 1; this->last_move_cmd = hoermann_action_none; }
            else if (pos & hoermann_state_closed) { this->last_endlage = 2; this->last_move_cmd = hoermann_action_none; }
            else this->zero_since = millis();
            this->pos_prev = pos;
          }
        }
        if (this->broadcast_status & hoermann_state_light_relay) this->light_bit_seen = true;
      }
    }
    // Slave status request (only 4 byte --> other indices of rx_data!)
    // 28 A1 20 2E
    if (this->rx_data[1] == UAP1_ADDR) {
      length = this->rx_data[2] & 0x0F;
      if (this->rx_data[3] == CMD_SLAVE_STATUS_REQUEST && length == 1 && calc_crc8(&this->rx_data[1], length + 3) == 0x00
          && (!this->inject_mode || this->next_action != hoermann_action_none)) {
        ESP_LOGVV(TAG, "Slave status request: %s", print_data(this->rx_data, 1, 5));
        ESP_LOGV(TAG, "->      Slave status request");
        counter = (this->rx_data[2] & 0xF0) + 0x10;
        this->tx_data[0] = UAP1_ADDR_MASTER;
        this->tx_data[1] = 0x03 | counter;
        this->tx_data[2] = CMD_SLAVE_STATUS_RESPONSE;
        this->tx_data[3] = (uint8_t)(this->next_action & 0xFF);
        this->tx_data[4] = (uint8_t)((this->next_action >> 8) & 0xFF);
        if (this->inject_mode && this->einwurf_v2) {
          uint32_t now = millis();
          if (this->pending_action != hoermann_action_none && this->pending_action != this->next_action) {
            this->v2_finish("replaced by new command");
          }
          if (this->pending_action == hoermann_action_none) {   // first attempt: remember initial status + measurability
            this->pending_tries = 0;
            this->pending_status0 = this->broadcast_status;
            const uint16_t s0 = this->pending_status0;
            this->vent_left = false;
            const bool mb = this->motion_bits_seen && !this->p_modus;   // P mode: never assume moving bits
            switch (this->next_action) {
              case hoermann_action_toggle_light: this->retryable = this->light_bit_seen && !this->p_modus; break;
              case hoermann_action_open:    this->retryable = mb || (s0 & hoermann_state_closed); break;
              case hoermann_action_close:   this->retryable = mb || (s0 & hoermann_state_open); break;
              case hoermann_action_venting: this->retryable = mb || (s0 & (hoermann_state_open | hoermann_state_closed)); break;
              default: this->retryable = false; break;   // never repeat stop/impulse
            }
          }
          this->pending_action = this->next_action;
          this->pending_time = now;
          this->pending_tries++;
          if (this->pending_action != hoermann_action_toggle_light && this->pending_action != hoermann_action_impulse) {
            this->last_move_cmd = this->pending_action;
            this->last_move_time = now;
          }
          this->fetch_ms = now - this->next_set_time;
          snprintf(this->msg_buf, sizeof(this->msg_buf), "%04X %s: sent (attempt %d, fetched after %u ms, status 0x%04X)",
                   this->pending_action, action_name(this->pending_action), this->pending_tries, (unsigned) this->fetch_ms,
                   this->pending_status0);
          this->msg_dirty = true;   // log/publish only in loop_slow, not before sending
        } else if (this->inject_mode) {
          this->pending_action = this->next_action;
          this->pending_time = millis();
          this->pending_status0 = this->broadcast_status;
          this->pending_tries++;
          ESP_LOGI(TAG, "Inject: command %04X sent (attempt %d)", this->next_action, this->pending_tries);
        }
        this->next_action = hoermann_action_none;
        this->tx_data[5] = calc_crc8(this->tx_data, 5);
        this->tx_length = 6;
        this->send_time = millis();
      }
    }
    // Update valid_broadcast if a non-default message is received
    if (!this->valid_broadcast && (this->rx_data[3] != 0 || this->rx_data[4] != 0)) {
      this->valid_broadcast = true;
      this->data_has_changed = true;
    }
#if ESPHOME_LOGLEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
    // just print the data
    if (this->byte_cnt >= 5) {
      ESP_LOGVV(TAG, "Just printed: %s", print_data(this->rx_data, 0, 5));
    }	
#endif
  }
}

void UAPBridge_esp::transmit() {
  ESP_LOGVV(TAG, "Transmit: %s", print_data(this->tx_data, 0, this->tx_length));
  if (this->rts_pin_ != nullptr) {
    this->rts_pin_->digital_write(true);// LOW(false) = listen, HIGH(true) = transmit
  }
  // Generate Sync break
  this->parent_->set_baud_rate(9600);
  this->parent_->set_data_bits(7);
  this->parent_->set_parity(esphome::uart::UARTParityOptions::UART_CONFIG_PARITY_NONE);
  this->parent_->set_stop_bits(1);
  this->parent_->load_settings(false);
  this->write_byte(0x00);
  this->flush();

  // Transmit
  this->parent_->set_baud_rate(19200);
  this->parent_->set_data_bits(8);
  this->parent_->set_parity(esphome::uart::UARTParityOptions::UART_CONFIG_PARITY_NONE);
  this->parent_->set_stop_bits(1);
  this->parent_->load_settings(false);
  this->write_array(this->tx_data, this->tx_length);
  this->flush();

  if (this->rts_pin_ != nullptr) {
    this->rts_pin_->digital_write(false);// LOW(false) = listen, HIGH(true) = transmit
  }

  ESP_LOGVV(TAG, "TX duration: %dms", millis() - this->send_time);
}
/**
 * Helper to set next Command and *not* skip Current Command before end was sent
 */
void UAPBridge_esp::set_command(bool cond, const hoermann_action_t command) {
  if (cond) {
    if (this->next_action != hoermann_action_none) {
      ESP_LOGW(TAG, "Last Command was not yet fetched by modbus! -- action cached %d", this->next_action);
      if (this->einwurf_v2) {
        snprintf(this->msg_buf, sizeof(this->msg_buf), "%04X %s: rejected, %04X %s still waiting to be fetched",
                 command, action_name(command), this->next_action, action_name(this->next_action));
        this->msg_dirty = true;
      }
    } else {
      this->next_action = command;
      this->next_set_time = millis();
      this->ignore_next_event = true;
    }
  }
}

void UAPBridge_esp::action_open() {
  ESP_LOGD(TAG, "Action: open called");
  this->set_command(this->state != hoermann_state_open, hoermann_action_open);
}

void UAPBridge_esp::action_close() {
  ESP_LOGD(TAG, "Action: close called");
  this->set_command(this->state != hoermann_state_closed, hoermann_action_close);
}

void UAPBridge_esp::action_stop() {
  ESP_LOGD(TAG, "Action: stop called");
  if (this->einwurf_v2) {
    // v2: some drives (P series) report no moving bits (status stays 0x0000 while moving) -> allow stop everywhere except in end positions
    this->set_command(this->state != hoermann_state_open && this->state != hoermann_state_closed, hoermann_action_stop);
    return;
  }
  this->set_command((this->state == hoermann_state_opening || this->state == hoermann_state_closing), hoermann_action_stop);
}

void UAPBridge_esp::action_venting() {
  ESP_LOGD(TAG, "Action: venting called");
  this->set_command(this->state != hoermann_state_venting, hoermann_action_venting);
}

void UAPBridge_esp::action_toggle_light() {
  ESP_LOGD(TAG, "Action: toggle light called");
  this->set_command(true, hoermann_action_toggle_light);
}

void UAPBridge_esp::action_impulse() {
  ESP_LOGD(TAG, "Action: impulse called");
  this->set_command(true, hoermann_action_impulse);
}

UAPBridge_esp::hoermann_state_t UAPBridge_esp::get_state() {
  return this->state;
}

std::string UAPBridge_esp::get_state_string() {
  return this->state_string;
}

void UAPBridge_esp::set_venting(bool state) {
  if (state) {
    this->action_venting();
  } else {
    this->action_close();
  }
  ESP_LOGD(TAG, "Venting state set to %s", state ? "ON" : "OFF");
}

void UAPBridge_esp::set_light(bool state) {
  this->set_command((this->light_enabled != state), hoermann_action_toggle_light);
  ESP_LOGD(TAG, "Light state set to %s", state ? "ON" : "OFF");
}

uint8_t UAPBridge_esp::calc_crc8(uint8_t *p_data, uint8_t length) {
  uint8_t i;
  uint8_t data;
  uint8_t crc = 0xF3;
  
  for(i = 0; i < length; i++) {
    /* XOR-in next input byte */
    data = *p_data ^ crc;
    p_data++;
    /* get current CRC value = remainder */
    crc = crc_table[data];
  }
  
  return crc;
}

char* UAPBridge_esp::print_data(uint8_t *p_data, uint8_t from, uint8_t to) {
	char temp[4];
	static char output[30];

  sprintf(output, "%5lu: ", millis() & 0xFFFFul);
	for (uint8_t i = from; i < to; i++) {
		sprintf(temp, "%02X ", p_data[i]);
		strcat(output, temp);
	}
	this->byte_cnt = 0;
  return &output[0];
}

void UAPBridge_esp::handle_state_change(hoermann_state_t new_state) {
  this->state = new_state;
  ESP_LOGV(TAG, "State changed from %s to %d", this->state_string.c_str(), new_state);
  switch (new_state) {
    case hoermann_state_open:
      this->state_string = "Offen";
      break;
    case hoermann_state_closed:
      this->state_string = "Geschlossen";
      break;
    case hoermann_state_opening:
      this->state_string = "Öffnen";
      break;
    case hoermann_state_closing:
      this->state_string = "Schließen";
      break;
    case hoermann_state_venting:
      this->state_string = "Lüften";
      break;
    case hoermann_state_stopped:
      this->state_string = "Gestoppt";
      break;
    default:
      this->state_string = "Fehler";
      break;
  }

  this->data_has_changed = true;
}

void UAPBridge_esp::update_boolean_state(const char * name, bool &current_state, bool new_state) {
  ESP_LOGV(TAG, "update_boolean_state: %s from %s to %s", name, current_state ? "true" : "false", new_state ? "true" : "false");
  if (current_state != new_state) {
    current_state = new_state;
    this->data_has_changed = true;
  }
}

}  // namespace uapbridge_esp
}  // namespace esphome
