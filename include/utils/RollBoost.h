#ifndef UTILS_ROLLBOOST_H_
#define UTILS_ROLLBOOST_H_

#include "utils/InputState.h"

#include <cstdint>

namespace Doncon::Utils {

// 連打(咚)增速 — pure output-layer drumroll boost, ported from the 2072 firmware.
//
// It never touches sensing/debounce; it only rewrites the emitted drum `triggered`
// bits just before the USB report is built. Two mechanisms share one window (ms),
// per device, 0 = disabled:
//
//   (A) side-flip: when the same don side is output again within the window during a
//       two-handed roll, re-emit it on the opposite side so the game scores the roll
//       as clean alternating L/R don.
//   (B) ka-suppress: while a don has been output within the window, drop ka output —
//       kills drumhead-vibration crosstalk that would otherwise break the roll.
//
// Mapping to itaiko pads: don_right = F = DON_F, don_left = J = DON_J,
// ka_left = D, ka_right = K (only don participates in the boost).
class RollBoost {
  public:
    static constexpr uint16_t kMaxWindowMs = 50; // RB_MAX

    // Transform `drum` in place. `window_ms` 0 disables (pass-through); values above
    // kMaxWindowMs are clamped. Call once per report cycle on a throw-away copy of the
    // input state so LEDs / display / sensor streaming keep the natural hits.
    void process(InputState::Drum &drum, uint32_t now, uint16_t window_ms) {
        const uint16_t win = window_ms > kMaxWindowMs ? kMaxWindowMs : window_ms;

        const bool f_now = drum.don_right.triggered; // DON_F
        const bool j_now = drum.don_left.triggered;  // DON_J

        if (win == 0) {
            // Disabled: leave output untouched, but keep edge memory current so a
            // later enable doesn't mistake a held pad for a fresh rising edge.
            m_prev_f = f_now;
            m_prev_j = j_now;
            m_don_fired = false;
            m_last_out_side = DON_NONE;
            return;
        }

        // (A) Resolve the emit side on each don rising edge (J before F, matching the
        // source order). The emit side is latched until the pad releases, so the
        // press/release pair of one hit always maps to the same side (no stuck keys).
        if (j_now && !m_prev_j) {
            m_j_emit = resolveDonSide(DON_J, now, win);
        }
        if (f_now && !m_prev_f) {
            m_f_emit = resolveDonSide(DON_F, now, win);
        }
        m_prev_j = j_now;
        m_prev_f = f_now;

        // Re-map each physical don to its latched emit side. Both pads can land on the
        // same side -> OR the trigger and keep the strongest analog (for MIDI velocity).
        InputState::Drum::Pad new_left{};  // DON_J side
        InputState::Drum::Pad new_right{}; // DON_F side
        contribute(new_left, new_right, drum.don_left, m_j_emit);
        contribute(new_left, new_right, drum.don_right, m_f_emit);
        drum.don_left = new_left;
        drum.don_right = new_right;

        // (B) Suppress ka while within the window of the last don output.
        if (m_don_fired && (now - m_last_out_time) <= win) {
            drum.ka_left.triggered = false;
            drum.ka_right.triggered = false;
        }
    }

  private:
    static constexpr uint16_t kBothWindowMs = 100; // ROLL_BOTH_WINDOW
    static constexpr uint8_t DON_F = 0;            // don_right
    static constexpr uint8_t DON_J = 1;            // don_left
    static constexpr uint8_t DON_NONE = 2;

    uint32_t m_last_out_time = 0;       // last don output time (A + B share this)
    uint8_t m_last_out_side = DON_NONE; // last don *emit* side (flip decision)
    bool m_don_fired = false;           // any don output yet (guards boot-time ka)
    uint8_t m_f_emit = DON_F;           // don_right pad's latched emit side
    uint8_t m_j_emit = DON_J;           // don_left pad's latched emit side

    uint32_t m_last_nat_f = 0, m_last_nat_j = 0; // last *natural* (real pad) hit times
    bool m_f_ever = false, m_j_ever = false;
    bool m_prev_f = false, m_prev_j = false; // previous-cycle triggered (edge detect)

    uint8_t resolveDonSide(uint8_t natural_side, uint32_t now, uint16_t win) {
        if (natural_side == DON_F) {
            m_last_nat_f = now;
            m_f_ever = true;
        } else {
            m_last_nat_j = now;
            m_j_ever = true;
        }

        // Gate: both don sides actually hit within kBothWindowMs => confirmed two-hand
        // roll (a one-hand mash on a single pad is left alone).
        const bool both_rolling = m_f_ever && m_j_ever &&                //
                                  (now - m_last_nat_f <= kBothWindowMs) && //
                                  (now - m_last_nat_j <= kBothWindowMs);

        uint8_t side = natural_side;
        if (both_rolling && m_last_out_side != DON_NONE &&        //
            (now - m_last_out_time) <= win &&                     //
            m_last_out_side == natural_side) {
            side = (natural_side == DON_F) ? DON_J : DON_F; // flip to the other side
        }

        m_last_out_side = side;
        m_last_out_time = now;
        m_don_fired = true;
        return side;
    }

    static void contribute(InputState::Drum::Pad &left, InputState::Drum::Pad &right,
                           const InputState::Drum::Pad &src, uint8_t emit_side) {
        if (!src.triggered) {
            return;
        }
        InputState::Drum::Pad &dst = (emit_side == DON_F) ? right : left;
        dst.triggered = true;
        if (src.analog >= dst.analog) {
            dst.analog = src.analog;
            dst.raw = src.raw;
            dst.duration_ms = src.duration_ms;
        }
    }
};

} // namespace Doncon::Utils

#endif // UTILS_ROLLBOOST_H_
