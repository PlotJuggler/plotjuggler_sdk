#pragma once
#include <string>
#include <vector>
#include "pj/engine/types.hpp"

namespace pj::engine {

struct TimeDomain {
  TimeDomainId id = 0;
  std::string name;
  Timestamp display_offset = 0;  // display_time = raw_time - display_offset
};

struct DatasetDescriptor {
  std::string source_name;
  TimeDomainId time_domain_id = 0;
};

struct DatasetInfo {
  DatasetId id = 0;
  std::string source_name;
  TimeDomain time_domain;
  std::vector<TopicId> topic_ids;
};

}  // namespace pj::engine
