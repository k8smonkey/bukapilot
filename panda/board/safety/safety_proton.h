// Ported from upstream kommuai/bukapilot:release_ka2 (panda/board/safety/safety_proton.h),
// adapted for KA1: this branch's Proton python controller (selfdrive/car/proton/*) transmits
// everything on a single bus (bus 0) -- there is no bus-0/bus-2 split like release_ka2's harness,
// so ACC_BUTTONS is listed on bus 0 here (release_ka2 has it on bus 2) and the STEERING_TORQUE
// entry (336) has been added for the ICC-only lateral torque-spoof fix.
//
// NOTE: this is a draft port, unverified on real hardware. release_ka2's proton_tx_hook is
// permissive (no per-message content validation, address/bus/length allowlist only via
// PROTON_TX_MSGS). Whether KA1's harness can actually suppress the real STEERING_TORQUE
// broadcast the way release_ka2's proton_fwd_hook suppresses LKAS/ACC on its bus-2 segment is
// still unconfirmed -- see PR description.
const CanMsg PROTON_TX_MSGS[] = {{432, 0, 8}, {643, 0, 8}, {336, 0, 8}};
bool using_stock_acc = false;

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
  UNUSED(addr);
  UNUSED(len);

  return tx;
}

static int proton_fwd_hook(int bus_num, int addr) {
  int bus_fwd = -1;
  if (bus_num == 0) {
    bus_fwd = 2;
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
  return BUILD_SAFETY_CFG(proton_rx_checks, PROTON_TX_MSGS);
}


const safety_hooks proton_hooks = {
  .init = proton_init,
  .rx = proton_rx_hook,
  .tx = proton_tx_hook,
  .fwd = proton_fwd_hook,
};
