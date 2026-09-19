// Ported from upstream kommuai/bukapilot:release_ka2 (panda/board/safety/safety_proton.h),
// adapted for KA1: this branch's Proton python controller (selfdrive/car/proton/*) transmits
// ADAS_LKAS/ACC_BUTTONS on bus 0 -- there is no bus split needed for those, matching
// release_ka2's LKAS assignment but not its ACC_BUTTONS (which release_ka2 has on bus 2).
//
// STEERING_TORQUE (336) is the exception: it flows car (bus 0) -> bus 2, the opposite
// direction from ADAS_LKAS/ACC_BUTTONS, so the device's spoofed replacement is sent on
// bus 2 (see selfdrive/car/proton/protoncan.py:create_steering_torque_spoof) and the real
// broadcast must be blocked from crossing bus 0 -> bus 2 for a few frames whenever the
// spoof is sent, exactly like the ADAS_LKAS/ACC_CMD block-on-bus-2 pattern below but in
// the opposite direction. Per maintainer guidance (2026-09-19): block for exactly 3 frames
// -- 1 frame causes stock-value flicker, always-blocking causes an ADAS error on the car.
// This mirrors kommuai/opendbc@001dda2a's proton_tq_tx_block_frames/PROTON_ACC_TX_BLOCK_MAX
// pattern for the newer opendbc.car.proton architecture.
//
// UNVERIFIED on real KA1 hardware: maintainer confirmed KA1/KA2 share the same relay
// connector/cable, but was not sure whether the panda board itself behaves identically
// device-side. Needs on-vehicle testing after a firmware rebuild+reflash.
const CanMsg PROTON_TX_MSGS[] = {{432, 0, 8}, {643, 0, 8}, {336, 2, 8}};
bool using_stock_acc = false;

#define PROTON_STEERING_TORQUE 336
#define PROTON_TQ_TX_BLOCK_MAX 3U
static uint8_t proton_tq_tx_block_frames = 0U;

RxCheck proton_rx_checks[] = {
};

static void proton_rx_hook(const CANPacket_t *to_push) {
  // proton is never at standstill
  vehicle_moving = true;
  controls_allowed = true;
  UNUSED(to_push);
}

static bool proton_tx_hook(const CANPacket_t *to_send) {
  bool tx = true;
  int addr = GET_ADDR(to_send);
  int len = GET_LEN(to_send);
  UNUSED(len);

  if (addr == PROTON_STEERING_TORQUE) {
    proton_tq_tx_block_frames = PROTON_TQ_TX_BLOCK_MAX;
  }

  return tx;
}

static int proton_fwd_hook(int bus_num, int addr) {
  int bus_fwd = -1;
  if (bus_num == 0) {
    bool is_tq_msg = (addr == PROTON_STEERING_TORQUE) && (proton_tq_tx_block_frames > 0U);
    if (is_tq_msg) {
      proton_tq_tx_block_frames--;
    }
    bus_fwd = is_tq_msg ? -1 : 2;
  }

  if (bus_num == 2) {
    bool is_lkas_msg = (addr == 432);
    bool is_acc_msg = (addr == 417) && !using_stock_acc;
    bool block_msg = is_lkas_msg || is_acc_msg;
    if (!block_msg) {
      bus_fwd = 0;
    }
  }

  return bus_fwd;
}

static safety_config proton_init(uint16_t param) {
  if (param == 2) using_stock_acc = true;
  proton_tq_tx_block_frames = 0U;
  return BUILD_SAFETY_CFG(proton_rx_checks, PROTON_TX_MSGS);
}


const safety_hooks proton_hooks = {
  .init = proton_init,
  .rx = proton_rx_hook,
  .tx = proton_tx_hook,
  .fwd = proton_fwd_hook,
};
