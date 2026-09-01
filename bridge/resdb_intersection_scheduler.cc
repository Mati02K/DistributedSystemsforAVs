#include "integration/omnet/resdb_intersection_scheduler.h"

#include "integration/omnet/resdb_conflict_matrix.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <unordered_set>

namespace resdb::omnet {
namespace {

bool IsQuietEntry(const ResdbVehicleEntry& e) {
  return e.cyber_status == 0 || e.sim_time_us == UINT64_MAX;
}

int CompareLaneQueueOrder(const ResdbVehicleEntry& a,
                          const ResdbVehicleEntry& b) {
  if (a.position_in_lane != b.position_in_lane)
    return static_cast<int>(a.position_in_lane) - static_cast<int>(b.position_in_lane);
  if (a.replica_id < b.replica_id) return -1;
  if (a.replica_id > b.replica_id) return 1;
  return 0;
}

// Two vehicles are in the same physical queue when they share an approach and,
// where they claim one, a physical lane. Queue order only means anything within
// such a queue: a left-turner in the inner lane is not behind a straight-goer
// beside it, and treating it as behind would serialise exactly the pair the
// second lane exists to run together.
//
// Vehicles that claim no lane (0xFF, every single-lane run) fall back to
// "same approach is same queue", which is what this always did.
bool SameQueue(const ResdbVehicleEntry& a, const ResdbVehicleEntry& b) {
  if (a.lane != b.lane) return false;
  if (a.physical_lane_index == kNoPhysicalLane ||
      b.physical_lane_index == kNoPhysicalLane) return true;
  return a.physical_lane_index == b.physical_lane_index;
}

bool AllSameLaneFrontPlaced(const ResdbVehicleEntry& candidate,
                            const std::vector<ResdbVehicleEntry>& view,
                            const std::unordered_set<int32_t>& placed) {
  for (const auto& v : view) {
    if (!SameQueue(v, candidate)) continue;
    if (CompareLaneQueueOrder(v, candidate) < 0 &&
        placed.find(v.replica_id) == placed.end()) {
      return false;
    }
  }
  return true;
}

bool SafeWithWholeBatch(const ResdbVehicleEntry& e,
                        const std::vector<ResdbVehicleEntry>& batch) {
  if (IsQuietEntry(e)) return false;
  for (const auto& b : batch) {
    if (!IsSafeToBatch(e.lane, e.direction, e.physical_lane_index,
                       b.lane, b.direction, b.physical_lane_index))
      return false;
  }
  return true;
}

}  // namespace

IntersectionScheduleResult BuildIntersectionSchedule(
    const ResdbProposeHdr& hdr,
    const std::vector<ResdbVehicleEntry>& entries) {
  IntersectionScheduleResult result;

  for (const auto& e : entries) {
    if (e.is_ambulance && e.cyber_status == 1) {
      result.ambulance_lane = static_cast<int>(e.lane);
      break;
    }
  }

  std::vector<ResdbVehicleEntry> ambulances;
  for (const auto& e : entries) {
    if (e.is_ambulance && e.cyber_status == 1) ambulances.push_back(e);
  }
  std::sort(ambulances.begin(), ambulances.end(),
            [](const ResdbVehicleEntry& a, const ResdbVehicleEntry& b) {
              if (a.position_in_lane != b.position_in_lane)
                return a.position_in_lane < b.position_in_lane;
              return a.replica_id < b.replica_id;
            });

  std::unordered_set<int32_t> priority_ids;
  std::vector<ResdbVehicleEntry> work_queue;
  for (const auto& ambulance : ambulances) {
    std::vector<ResdbVehicleEntry> blockers;
    for (const auto& v : entries) {
      // Only cars in the ambulance's own queue block it. One in the adjacent
      // physical lane of the same approach does not, and promoting it would
      // grant priority to a vehicle that was never in the way.
      if (!SameQueue(v, ambulance) || v.is_ambulance) continue;
      if (CompareLaneQueueOrder(v, ambulance) < 0) blockers.push_back(v);
    }
    std::sort(blockers.begin(), blockers.end(),
              [](const ResdbVehicleEntry& a, const ResdbVehicleEntry& b) {
                if (a.position_in_lane != b.position_in_lane)
                  return a.position_in_lane < b.position_in_lane;
                return a.replica_id < b.replica_id;
              });
    for (const auto& blocker : blockers) {
      if (priority_ids.insert(blocker.replica_id).second)
        work_queue.push_back(blocker);
    }
    if (priority_ids.insert(ambulance.replica_id).second)
      work_queue.push_back(ambulance);
  }

  std::vector<ResdbVehicleEntry> remaining_entries;
  for (const auto& e : entries) {
    if (priority_ids.find(e.replica_id) == priority_ids.end())
      remaining_entries.push_back(e);
  }
  std::sort(remaining_entries.begin(), remaining_entries.end(),
            [](const ResdbVehicleEntry& a, const ResdbVehicleEntry& b) {
              if (a.position_in_lane != b.position_in_lane)
                return a.position_in_lane < b.position_in_lane;
              if (a.replica_id != b.replica_id) return a.replica_id < b.replica_id;
              return a.sim_time_us < b.sim_time_us;
            });
  for (const auto& e : remaining_entries) work_queue.push_back(e);

  const uint32_t n = static_cast<uint32_t>(entries.size());
  std::vector<ResdbVehicleDecision> decisions(n);
  std::vector<std::vector<ResdbVehicleEntry>> batches_out;
  std::unordered_set<int32_t> placed;

  while (placed.size() < n) {
    const ResdbVehicleEntry* head_ptr = nullptr;
    for (const auto& cand : work_queue) {
      if (placed.count(cand.replica_id)) continue;
      if (!AllSameLaneFrontPlaced(cand, entries, placed)) continue;
      head_ptr = &cand;
      break;
    }
    if (!head_ptr) {
      std::cout << "[SCHEDULER] no schedulable head placed=" << placed.size()
                << "/" << n << "\n";
      for (const auto& e : entries) {
        if (placed.count(e.replica_id)) continue;
        batches_out.push_back({e});
        placed.insert(e.replica_id);
      }
      break;
    }

    ResdbVehicleEntry head = *head_ptr;
    std::vector<ResdbVehicleEntry> batch;
    batch.push_back(head);
    placed.insert(head.replica_id);

    if (IsQuietEntry(head)) {
      batches_out.push_back(std::move(batch));
      continue;
    }

    bool grew = false;
    do {
      grew = false;
      for (const auto& cand : work_queue) {
        if (placed.count(cand.replica_id)) continue;
        if (!AllSameLaneFrontPlaced(cand, entries, placed)) continue;
        if (IsQuietEntry(cand)) continue;
        if (!SafeWithWholeBatch(cand, batch)) continue;
        batch.push_back(cand);
        placed.insert(cand.replica_id);
        grew = true;
      }
    } while (grew);

    batches_out.push_back(std::move(batch));
  }

  // Check the schedule against the rule that produced it, before anyone acts on
  // it. Batch growth already guarantees this by construction, so a violation
  // means the greedy loop and the safety predicate have diverged -- which is
  // precisely the failure that would otherwise reach vehicles as a committed
  // order and be discovered by a collision. Cheap, and it turns a property
  // maintained by the shape of the code into one that is actually asserted.
  for (uint32_t bi = 0; bi < batches_out.size(); ++bi) {
    const auto& b = batches_out[bi];
    for (size_t i = 0; i < b.size(); ++i) {
      for (size_t j = i + 1; j < b.size(); ++j) {
        if (!IsSafeToBatch(b[i].lane, b[i].direction, b[i].physical_lane_index,
                           b[j].lane, b[j].direction, b[j].physical_lane_index)) {
          std::cout << "[SCHEDULER-UNSAFE-BATCH] epoch=" << hdr.epoch
                    << " batch=" << bi
                    << " a=r" << b[i].replica_id
                    << "(lane=" << (int)b[i].lane << " dir=" << (int)b[i].direction
                    << " plane=" << (int)b[i].physical_lane_index << ")"
                    << " b=r" << b[j].replica_id
                    << "(lane=" << (int)b[j].lane << " dir=" << (int)b[j].direction
                    << " plane=" << (int)b[j].physical_lane_index << ")\n";
        }
      }
    }
  }

  result.n_batches = static_cast<uint32_t>(batches_out.size());
  for (uint32_t bi = 0; bi < batches_out.size(); ++bi) {
    for (const auto& b : batches_out[bi]) {
      for (uint32_t i = 0; i < n; ++i) {
        if (entries[i].replica_id == b.replica_id)
          decisions[i] = {b.replica_id, bi};
      }
    }
  }

  result.order_bytes.assign(sizeof(ResdbOrderHdr) +
                                n * sizeof(ResdbVehicleDecision),
                            '\0');
  uint8_t* out = reinterpret_cast<uint8_t*>(&result.order_bytes[0]);
  ResdbOrderHdr ohdr{hdr.epoch, n, result.n_batches};
  std::memcpy(out, &ohdr, sizeof(ohdr));
  out += sizeof(ohdr);
  for (uint32_t i = 0; i < n; ++i) {
    std::memcpy(out, &decisions[i], sizeof(ResdbVehicleDecision));
    out += sizeof(ResdbVehicleDecision);
  }

  return result;
}

}  // namespace resdb::omnet
