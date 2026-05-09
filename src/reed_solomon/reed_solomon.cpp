// Explicit instantiations for ReedSolomonCodesT — see
// src/reed_solomon/reed_solomon.md for the algorithm description.

#include "boofcv_qr/reed_solomon.hpp"

namespace boofcv_qr {

template class ReedSolomonCodesT<std::uint8_t>;
template class ReedSolomonCodesT<std::uint16_t>;

}  // namespace boofcv_qr
