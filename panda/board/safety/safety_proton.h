// Ported from upstream kommuai/bukapilot:release_ka2 (panda/board/safety/safety_proton.h)
// and kommuai/opendbc@001dda2a (opendbc/safety/modes/proton.h), rewritten for the older
// panda safety-hook API used on this branch (addr_checks / safety_hooks returning int,
// not the newer RxCheck / safety_config / BUILD_SAFETY_CFG API those references use --
// this branch's panda/board/safety_declarations.h predates that refactor). See
// safety_mazda.h in this same directory for the reference pattern this follows.
//
// STEERING_TORQUE (336) flows car (bus 0) -> bus 2, the opposite direction from
// ADAS_LKAS/ACC_BUTTONS, so the device's spoofed replacement is sent on bus 2 (see
// selfdrive/car/proton/protoncan.py:create_steering_torque_spoof) and the real broadcast
// must be blocked from crossing bus 0 -> bus 2 for a few frames whenever the spoof is
// sent. Per maintainer guidance (2026-09-19): block for exactly 3 frames -- 1 frame
// causes stock-value flicker, always-blocking causes an ADAS error on the car.
//
// UNVERIFIED on real KA1 hardware: maintainer confirmed KA1/KA2 share the same relay
// connector/cable, but was not sure whether the panda board itself behaves identically
// device-side. Needs on-vehicle testing after this firmware is flashed.

#define PROTON_ADAS_LKAS 432
#define PROTON_ACC_CMD 417
#define PROTON_ACC_BUTTONS 643
#define PROTON_STEERING_TORQUE 336

#define PROTON_MAIN 0
#define PROTON_CAM 2

#define PROTON_TQ_TX_BLOCK_MAX 3U
static uint8_t proton_tq_tx_block_frames = 0U;
bool using_stock_acc = false;

const CanMsg PROTON_TX_MSGS[] = {
  {PROTON_ADAS_LKAS, PROTON_MAIN, 8},
  {PROTON_ACC_BUTTONS, PROTON_MAIN, 8},
  {PROTON_STEERING_TORQUE, PROTON_CAM, 8},
};

const addr_checks proton_rx_checks = {NULL, 0};

static int proton_rx_hook(CANPacket_t *to_push) {
  // proton is never at standstill
  vehicle_moving = true;
  controls_allowed = true;
  UNUSED(to_push);
  return true;
}

static int proton_tx_hook(CANPacket_t *to_send) {
  int tx = 1;
  int addr = GET_ADDR(to_send);

  if (!msg_allowed(to_send, PROTON_TX_MSGS, sizeof(PROTON_TX_MSGS) / sizeof(PROTON_TX_MSGS[0]))) {
    tx = 0;
  }

  if (addr == PROTON_STEERING_TORQUE) {
    proton_tq_tx_block_frames = PROTON_TQ_TX_BLOCK_MAX;
  }

  return tx;
}

static int proton_fwd_hook(int bus_num, CANPacket_t *to_fwd) {
  int bus_fwd = -1;
  int addr = GET_ADDR(to_fwd);

  if (bus_num == PROTON_MAIN) {
    bool is_tq_msg = (addr == PROTON_STEERING_TORQUE) && (proton_tq_tx_block_frames > 0U);
    if (is_tq_msg) {
      proton_tq_tx_block_frames--;
    }
    bus_fwd = is_tq_msg ? -1 : PROTON_CAM;
  } else if (bus_num == PROTON_CAM) {
    bool is_lkas_msg = (addr == PROTON_ADAS_LKAS);
    bool is_acc_msg = (addr == PROTON_ACC_CMD) && !using_stock_acc;
    bool block_msg = is_lkas_msg || is_acc_msg;
    if (!block_msg) {
      bus_fwd = PROTON_MAIN;
    }
  } else {
    // no forward
  }

  return bus_fwd;
}

static const addr_checks* proton_init(int16_t param) {
  if (param == 2) {
    using_stock_acc = true;
  }
  proton_tq_tx_block_frames = 0U;
  controls_allowed = false;
  relay_malfunction_reset();
  return &proton_rx_checks;
}

const safety_hooks proton_hooks = {
  .init = proton_init,
  .rx = proton_rx_hook,
  .tx = proton_tx_hook,
  .tx_lin = nooutput_tx_lin_hook,
  .fwd = proton_fwd_hook,
};
