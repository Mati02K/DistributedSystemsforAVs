#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <omnetpp.h>

#include "v2vbft/crypto/CryptoAuth.h"
#include "v2vbft/protocol/Primitives.h"

// ── Arrival-certificate protocol types ───────────────────────────────────────
//
// The ANN(1) → ECHO(4) → CERT(5) exchange that establishes, with f+1 signed
// witnesses, that a vehicle really is where it claims to be before its entry
// may enter a consensus proposal.
//
// Extracted verbatim from ResDBIntersectionApp's private nested types.

namespace v2vbft {

// One vehicle's claim about itself, as carried in a proposal.
struct VehicleState {
    std::string vehicleId;
    std::string lane;
    int         positionInLane  = 1;
    Direction   direction       = DIR_STRAIGHT;
    bool        isAmbulance     = false;
    uint64_t    arrival_time_us = 0;
};

// Type 1: a vehicle announces its arrival at the junction.
struct ArrivalAnnouncement {
    std::string          carId;
    std::string          laneId;
    std::string          lane;
    int                  positionInLane      = 1;
    Direction            direction           = DIR_STRAIGHT;
    bool                 isAmbulance         = false;
    double               claimedArrivalTime  = 0.0;
    int                  epoch               = 0;
    // Where the vehicle says it is across the lane, in centimetres on the
    // lane-normal axis. Only meaningful under ADJACENT_LATERAL, where an
    // approach has two parallel lanes and "which lane" is a claim a witness can
    // check against a continuous observation rather than take on trust.
    // physicalLaneIndex is the discrete claim that lateralClaimCm must project
    // to; -1 means the vehicle is not making one.
    int32_t              lateralClaimCm      = 0;
    int                  physicalLaneIndex   = -1;
    std::vector<uint8_t> ambulanceCertBytes;
    std::vector<uint8_t> ambulanceSigBytes;
    std::vector<uint8_t> signature;
};

// Type 4: one replica's signed witness that an announcement matches ground truth.
struct ArrivalEcho {
    int         echoingReplicaId = -1;
    std::string targetCarId;
    std::string lane;
    int         positionInLane = 1;
    Direction   direction      = DIR_STRAIGHT;
    bool        isAmbulance    = false;
    int         epoch          = 0;
    uint8_t     signerPubKey[CRYPTO_PUBKEY_BYTES] = {};
    uint8_t     signature[CRYPTO_SIG_MAX_BYTES]   = {};
    uint8_t     signatureLen = 0;
};

// Type 5: f+1 echoes assembled — the certificate a proposal may cite.
struct ArrivalCert {
    std::string              carId;
    std::string              lane;
    int                      positionInLane = 1;
    Direction                direction      = DIR_STRAIGHT;
    bool                     isAmbulance    = false;
    int                      epoch          = 0;
    // Carried through from the announcement this cert attests, so a follower can
    // check a proposal's lane claim against certified state rather than against
    // the leader's word. Defaults mean "not claimed", which is the normal case
    // under CATEGORICAL_CARDINAL where an approach has a single lane.
    int32_t                  lateralClaimCm    = 0;
    int                      physicalLaneIndex = -1;
    // Hash of the arrival claim these echoes were collected against. The
    // distance round binds its attestation to this, so a distance cert cannot
    // be transplanted onto a different arrival claim by the same vehicle.
    std::array<uint8_t, 32>  claimHash{};
    std::vector<ArrivalEcho> echoes;
};

// ── Stopped-distance certificate (types 18/19/20) ────────────────────────────
// The second certificate round. An arrival cert says a vehicle is on the
// approach it claims; it says nothing about how far down the queue it is, and
// position_in_lane was previously the vehicle's own unchecked assertion. A
// vehicle that overstates its position crosses ahead of one that does not.
//
// So a stopped vehicle attests its distance to the stop line, witnesses gate
// that against their own noisy observation of the same distance, and f+1
// signed echoes make it a certificate. Ranks are then derived from certified
// distances rather than from claims.

// Type 18: the stopped vehicle's own signed distance claim, bound to the
// arrival claim it continues (earlyClaimHash) so the two cannot be mixed.
struct StoppedDistanceAttestation {
    std::string             targetCarId;
    int                     epoch            = 0;
    std::array<uint8_t, 32> earlyClaimHash{};
    int32_t                 distanceToStopCm = 0;
    std::vector<uint8_t>    signature;
};

// Type 19: one witness's signed agreement, bound to the exact attestation it
// saw (attestationHash) so an echo cannot be replayed onto a different claim.
struct StoppedDistanceEcho {
    int                     echoingReplicaId = -1;
    std::string             targetCarId;
    int                     epoch            = 0;
    std::array<uint8_t, 32> earlyClaimHash{};
    std::array<uint8_t, 32> attestationHash{};
    int32_t                 distanceToStopCm = 0;
    uint8_t                 signerPubKey[CRYPTO_PUBKEY_BYTES] = {};
    uint8_t                 signature[CRYPTO_SIG_MAX_BYTES]   = {};
    uint8_t                 signatureLen = 0;
};

// Type 20: the attestation plus its f+1 echoes.
struct StoppedDistanceCert {
    StoppedDistanceAttestation       attestation;
    std::vector<StoppedDistanceEcho> echoes;
};

// ── Discovery round ──────────────────────────────────────────────────────────
// Tracks how long to keep listening for new arrivals before closing the round
// and proposing. Closing too early drops a vehicle; too late stalls everyone.

enum class DiscoveryState {
    INACTIVE,
    COLLECTING,
    DRAINING_CERTS,
    COMPLETE,
};

enum class LocalCertState {
    NOT_ASSEMBLED,
    QUEUED,
    AIRED,
};

enum class DiscoveryCloseReason {
    NONE,
    STABILIZED,   // no new intent heard for the stabilisation window
    DEADLINE,     // hard cap reached
};

struct DiscoveryRound {
    DiscoveryState state = DiscoveryState::INACTIVE;
    uint32_t epoch = 0;
    omnetpp::simtime_t lastNewIntentAt = -1;
    omnetpp::simtime_t collectionStartedAt = SIMTIME_ZERO;
    LocalCertState localCert = LocalCertState::NOT_ASSEMBLED;
    DiscoveryCloseReason closeReason = DiscoveryCloseReason::NONE;
    // The distance certificate's own lifecycle, tracked separately from the
    // arrival one because discovery closes on arrivals first and distances
    // second. QUEUED vs AIRED matters for the same reason it does for the
    // arrival cert: a round must not complete while its own certificate is
    // still sitting in the transmit queue.
    LocalCertState localDistanceCert = LocalCertState::NOT_ASSEMBLED;

    void reset(uint32_t newEpoch, omnetpp::simtime_t now)
    {
        state = DiscoveryState::COLLECTING;
        epoch = newEpoch;
        lastNewIntentAt = now;
        collectionStartedAt = SIMTIME_ZERO;
        localCert = LocalCertState::NOT_ASSEMBLED;
        closeReason = DiscoveryCloseReason::NONE;
        localDistanceCert = LocalCertState::NOT_ASSEMBLED;
    }

    bool localCertAssembled() const
    {
        return localCert != LocalCertState::NOT_ASSEMBLED;
    }

    bool localCertAired() const
    {
        return localCert == LocalCertState::AIRED;
    }
};

} // namespace v2vbft
