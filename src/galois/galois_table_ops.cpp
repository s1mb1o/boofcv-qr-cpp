// Explicit instantiations for GaliosFieldTableOpsT — the template lives in
// the header but is instantiated here so its body is compiled exactly once
// per element type. See src/galois/galois_table_ops.md for the algorithm.

#include "boofcv_qr/galois_table_ops.hpp"

namespace boofcv_qr {

template class GaliosFieldTableOpsT<std::uint8_t>;
template class GaliosFieldTableOpsT<std::uint16_t>;

}  // namespace boofcv_qr
