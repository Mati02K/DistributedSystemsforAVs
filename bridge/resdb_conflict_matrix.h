#pragma once

// Which pairs of vehicles may cross the junction at the same time.
//
// This is the one definition. It used to exist twice — once in the bridge's
// scheduler, which decides the batches, and once in the Veins app's
// detectUnsafeBatch(), which checks after the fact whether a committed batch was
// safe — with a comment on each asking the next person to keep them in lockstep.
// Two copies of a safety rule kept in step by convention is a defect waiting for
// the day someone edits one, so both now include this header instead. The
// after-the-fact checker is only meaningful if it disagrees with the scheduler
// for the right reason: because a Byzantine leader lied about a vehicle's lane,
// never because the two tables drifted.
//
// Header-only, and reachable from both sides without build changes: the app
// already includes "integration/omnet/resdb_omnet_bridge.h", and setup.sh
// symlinks bridge/ to $(RESDB_ROOT)/integration/omnet.

#include <cstdint>

namespace resdb::omnet {

// Encodings, shared with ResdbVehicleEntry.
//   lane      0=N 1=S 2=E 3=W          (the cardinal approach)
//   direction 0=straight 1=left 2=right 3=unknown
//   physical  0=outer 1=inner, or kNoPhysicalLane when the vehicle claimed none
inline constexpr uint8_t kNoPhysicalLane = 0xFF;

// Cross-approach safe pairs, stored once per unordered pair.
//
// Rows 1-2   opposing through movements.
// Rows 3-8   every pair of distinct approaches both turning right: a right turn
//            leaves immediately and never reaches the middle of the junction.
// Rows 9-12  a right turn alongside the through movement it does not cross.
//            Deliberately absent: right + a *perpendicular* through, which is a
//            conservative choice rather than a geometric impossibility.
// Rows 13-14 the protected-left pairs. Two opposing lefts exit into different
//            roads and never cross, which is why a signalised junction runs them
//            in one phase. Without these no left turn batched with anything at
//            all, and under mixed turn demand every left-turner crossed alone.
//
// Direction 3 (unknown) appears in no row, so a vehicle whose turn cue f+1
// witnesses could not agree on can never be batched with anything.
inline constexpr uint8_t kSafeCrossApproach[14][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {0, 2, 1, 2}, {0, 2, 2, 2},
    {0, 2, 3, 2}, {1, 2, 2, 2}, {1, 2, 3, 2}, {2, 2, 3, 2},
    {0, 2, 1, 0}, {1, 2, 0, 0}, {2, 2, 3, 0}, {3, 2, 2, 0},
    {0, 1, 1, 1}, {2, 1, 3, 1},
};

// May these two vehicles cross together?
//
// Symmetric in its two arguments by construction: the table stores each pair
// once and both orientations are tested. Default is deny.
inline bool IsSafeToBatch(uint8_t lane_a, uint8_t dir_a, uint8_t physical_a,
                          uint8_t lane_b, uint8_t dir_b, uint8_t physical_b) {
    if (lane_a == lane_b) {
        // Same approach. With one lane per approach this is always a refusal,
        // and it still is whenever either vehicle claims no physical lane —
        // which is what every single-lane run sends, so that path is unchanged.
        if (physical_a == kNoPhysicalLane || physical_b == kNoPhysicalLane)
            return false;
        // Same physical lane: one is behind the other. Queue order, not conflict.
        if (physical_a == physical_b) return false;
        // Different lanes of the same approach. They enter the junction from
        // different points, so the only way they can conflict is by leaving
        // through the same exit — which, since the exit is determined by the
        // turn, means having the same direction. Anything else diverges.
        //
        // On the current two-lane network this cannot happen: lane 0 carries
        // straight and right, lane 1 carries left, so a same-approach pair never
        // shares a direction. The check costs nothing and keeps the rule correct
        // for a network that puts two straight lanes on one approach.
        return dir_a != dir_b;
    }

    for (const auto& p : kSafeCrossApproach) {
        if ((lane_a == p[0] && dir_a == p[1] && lane_b == p[2] && dir_b == p[3]) ||
            (lane_a == p[2] && dir_a == p[3] && lane_b == p[0] && dir_b == p[1])) {
            return true;
        }
    }
    return false;
}

}  // namespace resdb::omnet
