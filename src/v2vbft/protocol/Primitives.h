#pragma once

#include <string>

// ── Protocol vocabulary shared by every component ────────────────────────────
//
// The bottom of the protocol type layering: these depend on nothing and are
// used by all of ArrivalTypes.h, RollbackTypes.h and OrderCandidate.h.
//
// Extracted verbatim from ResDBIntersectionApp, where they were private nested
// types. Nesting them in the module class meant the arrival protocol, the
// rollback protocol and the transport could not name a shared type without
// naming the whole god class.

namespace v2vbft {

// Which way a vehicle intends to leave the junction.
//
// DIR_UNKNOWN is not a fourth intent — it is the absence of one. Under imperfect
// perception a witness may fail to read a vehicle's turn signal, so a direction
// is only certified when f+1 echoes independently observed the same cue. Short of
// that the cert carries DIR_UNKNOWN, and because the executor's kSafe table has
// no row for it, such a vehicle is structurally forced into a singleton batch
// rather than being guessed into someone else's.
enum Direction { DIR_STRAIGHT = 0, DIR_LEFT = 1, DIR_RIGHT = 2, DIR_UNKNOWN = 3 };

// What a witness believes it saw of a vehicle's maneuver cue (its turn signal).
//
// Distinct from Direction on purpose: Direction is what a vehicle *declares* and
// what consensus certifies, ObservedCue is one witness's noisy reading of the
// physical blinker. They are compared, never conflated -- eligibleDirection()
// downgrades a declaration to DIR_UNKNOWN unless enough independent cues agree.
enum class ObservedCue : uint8_t {
    STRAIGHT = 0,
    LEFT     = 1,
    RIGHT    = 2,
    UNKNOWN  = 3,
};

// ── Byzantine fault injection types ──────────────────────────────────────────
// Selected per replica via NED params; see ResDBDecision.cc for the injection
// sites and the detector that is expected to catch each one.
enum ByzantineType {
    BYZANTINE_HONEST         = 0,  // Normal behavior
    BYZANTINE_FALSE_LANE     = 1,  // Claims wrong lane in ARRIVAL_ANNOUNCE
    BYZANTINE_INVALID_SIG    = 2,  // Attaches garbage signature bytes
    BYZANTINE_EQUIVOCATOR    = 3,  // Sends different direction to different peers
    BYZANTINE_SILENT_PRIMARY          = 4,  // Primary suppresses proposeAll() — triggers VC
    BYZANTINE_BAD_PROPOSAL            = 5,  // Primary corrupts n_vehicles — PreVerify rejects
    BYZANTINE_FAKE_AMBULANCE          = 6,  // Primary flips is_ambulance 0→1 for non-ambulance car — PreVerify Check 10 rejects
    BYZANTINE_FAKE_AMBULANCE_FOLLOWER = 7,  // Follower claims isAmbulance=true without cert — cert gate catches this
    BYZANTINE_TAMPER_LANE             = 8,  // Primary: quiet real S car + reassign E car's lane to S → scheduler batches N+E simultaneously → CRASH
    // 9–12 attack the perception-derived fields specifically: each tampers with a
    // value the leader does not own, and each is caught by a cert-backed
    // pre-verify check rather than by a structural one.
    BYZANTINE_UPGRADE_UNKNOWN_DIRECTION = 9,  // Primary rewrites SIGNED-UNKNOWN as declared STRAIGHT — Check 10 rejects
    BYZANTINE_TAMPER_DISTANCE_RANK      = 10, // Primary rewrites the certificate-derived queue rank — Check 10 rejects
    BYZANTINE_TAMPER_PHYSICAL_LANE      = 11, // Primary rewrites certified physical-lane/lateral fields — Check 10 rejects
    BYZANTINE_SUPPRESS_CERTS            = 12, // Primary marks f+1 certified entries QUIET — Check 9 rejects
};

// ── Consensus phases ─────────────────────────────────────────────────────────
// A vehicle advances IDLE → COLLECTING_CERTS → WAITING_FOR_CLEARANCE →
// PULLING_FORWARD → EXECUTING → DEPARTED, and never moves backwards.
enum ConsensusPhase {
    IDLE,
    COLLECTING_CERTS,      // Cars broadcast ARRIVAL_ANNOUNCE; replicas echo; f+1 certs assembled
    WAITING_FOR_CLEARANCE, // Waiting for clearance from intersection controller
    PULLING_FORWARD,       // Pulling forward to stop line
    EXECUTING,             // Cars crossing intersection
    DEPARTED,              // Car has crossed intersection (zombie mode)
};

// Outcome of checking a claimed announcement against TraCI ground truth.
struct VerificationResult {
    bool        isValid  = false;
    std::string reason;
    std::string actualLaneId;
    double      actualPosition = 0.0;
};

} // namespace v2vbft
