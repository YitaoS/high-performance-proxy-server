#include "cache.hpp"

bool CachedResponse::operator==(const CachedResponse & other) const {
  return must_revalidate == other.must_revalidate && e_tag == other.e_tag &&
         status_code == other.status_code && status_message == other.status_message &&
         server == other.server && content_type == other.content_type &&
         body == other.body && expiration_time == other.expiration_time;
}
