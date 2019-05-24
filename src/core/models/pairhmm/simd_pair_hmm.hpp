// Copyright (c) 2015-2019 Daniel Cooke and Gerton Lunter
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef _pair_hmm_hpp
#define _pair_hmm_hpp

#if __GNUC__ >= 6
    #pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

#include <cstddef>
#include <algorithm>
#include <iterator>
#include <cassert>
#include <emmintrin.h>
#include <immintrin.h>

#include <boost/container/small_vector.hpp>

namespace octopus { namespace hmm { namespace simd {

template <typename InstructionSetPolicy>
class PairHMM : private InstructionSetPolicy
{
    // Types
    using VectorType = typename InstructionSetPolicy::VectorType;
    using ScoreType  = typename InstructionSetPolicy::ScoreType;
    
    using SmallVector = boost::container::small_vector<VectorType, 10000>;
    
    // Methods
    using InstructionSetPolicy::vectorise;
    using InstructionSetPolicy::vectorise_reverse;
    using InstructionSetPolicy::vectorise_reverse_lshift;
    using InstructionSetPolicy::vectorise_zero_set_last;
    using InstructionSetPolicy::_extract;
    using InstructionSetPolicy::_insert;
    using InstructionSetPolicy::_add;
    using InstructionSetPolicy::_and;
    using InstructionSetPolicy::_andnot;
    using InstructionSetPolicy::_or;
    using InstructionSetPolicy::_cmpeq;
    using InstructionSetPolicy::_min;
    using InstructionSetPolicy::_max;
    
    // Constants
    constexpr static int score_bytes = sizeof(ScoreType);
    constexpr static int band_size = sizeof(VectorType) / score_bytes;
    constexpr static ScoreType infinity = 0x7800;
    constexpr static int trace_bits = 2;
    constexpr static ScoreType n_score = 2 << trace_bits;
    
    constexpr static int max_n_quality {64}; // what does 64 mean?
    constexpr static int bitmask {0x8000}; // ?
    
    constexpr static int match_label  {0};
    constexpr static int insert_label {1};
    constexpr static int delete_label {3};
    
    const VectorType _inf = vectorise(infinity);
    const VectorType _nscore_m_inf = vectorise(n_score - infinity);
    const VectorType _n = vectorise('N');
    const VectorType _three = vectorise(3);
    
    template <int idx>
    auto _left_shift(const VectorType& vec) const noexcept { return InstructionSetPolicy::template _left_shift<idx>(vec); }
    template <int idx>
    auto _right_shift(const VectorType& vec) const noexcept { return InstructionSetPolicy::template _right_shift<idx>(vec); }
    template <int idx>
    auto _left_shift_words(const VectorType& vec) const noexcept { return InstructionSetPolicy::template _left_shift_words<idx>(vec); }
    
    template <int shift>
    void update_gap_penalty(VectorType& current, const std::int8_t* source, const std::size_t gap_idx) const noexcept
    {
        current = _insert(_right_shift<score_bytes>(current), source[gap_idx] << shift, band_size - 1);
    }
    template <int shift>
    void update_gap_penalty(VectorType& current, const short source, const std::size_t gap_idx) const noexcept {}

public:
    constexpr static char gap_label = '-';
    
    template <typename OpenPenalty,
              typename ExtendPenalty>
    int align(const char* truth,
              const char* target,
              const std::int8_t* qualities,
              const int truth_len,
              const int target_len,
              const OpenPenalty gap_open,
              const ExtendPenalty gap_extend,
              short nuc_prior) const noexcept
    {
        assert(truth_len > band_size && (truth_len == target_len + 2 * band_size - 1));
        auto _m1 = _inf, _i1 = _inf, _d1 = _inf, _m2 = _inf, _i2 = _inf, _d2 = _inf;
        const auto _nuc_prior  = vectorise(nuc_prior << trace_bits);
        auto _initmask  = vectorise_zero_set_last(-1);
        auto _initmask2 = vectorise_zero_set_last(-bitmask);
        auto _truthwin     = vectorise_reverse(truth);
        auto _targetwin    = _m1;
        auto _qualitieswin = vectorise(max_n_quality << trace_bits);
        auto _gap_open     = vectorise_reverse_lshift(gap_open, trace_bits);
        auto _gap_extend   = vectorise_reverse_lshift(gap_extend, trace_bits);
        auto _truthnqual   = _add(_and(_cmpeq(_truthwin, _n), _nscore_m_inf), _inf);
        ScoreType minscore {infinity};
        for (int s {0}; s <= 2 * (target_len + band_size); s += 2) {
            // truth is current; target needs updating
            _targetwin    = _left_shift<score_bytes>(_targetwin);
            _qualitieswin = _left_shift<score_bytes>(_qualitieswin);
            if (s / 2 < target_len) {
                _targetwin    = _insert(_targetwin, target[s / 2], 0);
                _qualitieswin = _insert(_qualitieswin, qualities[s / 2] << trace_bits, 0);
            } else {
                _targetwin    = _insert(_targetwin, '0', 0);
                _qualitieswin = _insert(_qualitieswin, max_n_quality << trace_bits, 0);
            }
            // S even
            _m1 = _or(_initmask2, _andnot(_initmask, _m1));
            _m2 = _or(_initmask2, _andnot(_initmask, _m2));
            _m1 = _min(_m1, _min(_i1, _d1));
            if (s / 2 >= target_len) {
                minscore = std::min(static_cast<decltype(minscore)>(_extract(_m1, std::max(0, s / 2 - target_len))), minscore);
            }
            _m1 = _add(_m1, _min(_andnot(_cmpeq(_targetwin, _truthwin), _qualitieswin), _truthnqual));
            _d1 = _min(_add(_d2, _gap_extend), _add(_min(_m2, _i2), _right_shift<score_bytes>(_gap_open))); // allow I->D
            _d1 = _insert(_left_shift<score_bytes>(_d1), infinity, 0);
            _i1 = _add(_min(_add(_i2, _gap_extend), _add(_m2, _gap_open)), _nuc_prior);
            // S odd
            // truth needs updating; target is current
            const auto pos = band_size + s / 2;
            const bool pos_in_range {pos < truth_len};
            const char base {pos_in_range ? truth[pos] : 'N'};
            _truthwin   = _insert(_right_shift<score_bytes>(_truthwin), base, band_size - 1);
            _truthnqual = _insert(_right_shift<score_bytes>(_truthnqual), base == 'N' ? n_score : infinity, band_size - 1);
            const auto gap_idx = pos_in_range ? pos : truth_len - 1;
            update_gap_penalty<score_bytes>(_gap_open, gap_open, gap_idx);
            update_gap_penalty<score_bytes>(_gap_extend, gap_extend, gap_idx);
            _initmask  = _left_shift<score_bytes>(_initmask);
            _initmask2 = _left_shift<score_bytes>(_initmask2);
            _m2 = _min(_m2, _min(_i2, _d2));
            if (s / 2 >= target_len) {
                minscore = std::min(static_cast<decltype(minscore)>(_extract(_m2, s / 2 - target_len)), minscore);
            }
            _m2 = _add(_m2, _min(_andnot(_cmpeq(_targetwin, _truthwin), _qualitieswin), _truthnqual));
            _d2 = _min(_add(_d1, _gap_extend), _add(_min(_m1, _i1), _gap_open)); // allow I->D
            _i2 = _insert(_add(_min(_add(_right_shift<score_bytes>(_i1), _gap_extend),
                                         _add(_right_shift<score_bytes>(_m1), _gap_open)), _nuc_prior), infinity, band_size - 1);
        }
        return (minscore + bitmask) >> trace_bits;
    }
    
    template <typename OpenPenalty,
              typename ExtendPenalty>
    int
    align(const char* truth,
          const char* target,
          const std::int8_t* qualities,
          const int truth_len,
          const int target_len,
          const OpenPenalty gap_open,
          const ExtendPenalty gap_extend,
          short nuc_prior,
          int& first_pos,
          char* align1,
          char* align2) noexcept
    {
        assert(truth_len > band_size && (truth_len == target_len + 2 * band_size - 1));
        auto _m1 = _inf, _i1 = _inf, _d1 = _inf, _m2 = _inf, _i2 = _inf, _d2 = _inf;
        const auto _nuc_prior = vectorise(nuc_prior << trace_bits);
        auto _initmask  = vectorise_zero_set_last(-1);
        auto _initmask2 = vectorise_zero_set_last(-bitmask);
        auto _truthwin     = vectorise_reverse(truth);
        auto _targetwin    = _m1;
        auto _qualitieswin = vectorise(max_n_quality << trace_bits);
        auto _gap_open     = vectorise_reverse_lshift(gap_open, trace_bits);
        auto _gap_extend   = vectorise_reverse_lshift(gap_extend, trace_bits);
        auto _truthnqual   = _add(_and(_cmpeq(_truthwin, _n), _nscore_m_inf), _inf);
        SmallVector _backpointers(2 * (truth_len + band_size));
        ScoreType minscore {infinity}, cur_score;
        int s, minscoreidx {-1};
        for (s = 0; s <= 2 * (target_len + band_size); s += 2) {
            // truth is current; target needs updating
            _targetwin    = _left_shift<score_bytes>(_targetwin);
            _qualitieswin = _left_shift<score_bytes>(_qualitieswin);
            if (s / 2 < target_len) {
                _targetwin    = _insert(_targetwin, target[s / 2], 0);
                _qualitieswin = _insert(_qualitieswin, qualities[s / 2] << trace_bits, 0);
            } else {
                _targetwin    = _insert(_targetwin, '0', 0);
                _qualitieswin = _insert(_qualitieswin, max_n_quality << trace_bits, 0);
            }
            // S even
            _m1 = _or(_initmask2, _andnot(_initmask, _m1));
            _m2 = _or(_initmask2, _andnot(_initmask, _m2));
            _m1 = _min(_m1, _min(_i1, _d1));
            if (s / 2 >= target_len) {
                cur_score = _extract(_m1, s / 2 - target_len);
                if (cur_score < minscore) {
                    minscore = cur_score;
                    minscoreidx = s;
                }
            }
            _m1 = _add(_m1, _min(_andnot(_cmpeq(_targetwin, _truthwin), _qualitieswin), _truthnqual));
            _d1 = _min(_add(_d2, _gap_extend), _add(_min(_m2, _i2), _right_shift<score_bytes>(_gap_open))); // allow I->D
            _d1 = _insert(_left_shift<score_bytes>(_d1), infinity, 0);
            _i1 = _add(_min(_add(_i2, _gap_extend), _add(_m2, _gap_open)), _nuc_prior);
            _backpointers[s] = _or(_or(_and(_three, _m1), _left_shift_words<2 * insert_label>(_and(_three, _i1))),
                                            _left_shift_words<2 * delete_label>(_and(_three, _d1)));
            // set state labels
            _m1 = _andnot(_three, _m1);
            _i1 = _or(_andnot(_three, _i1), _right_shift<1>(_three));
            _d1 = _or(_andnot(_three, _d1), _three);
            // S odd
            // truth needs updating; target is current
            const auto pos = band_size + s / 2;
            const bool pos_in_range {pos < truth_len};
            const char base {pos_in_range ? truth[pos] : 'N'};
            _truthwin   = _insert(_right_shift<score_bytes>(_truthwin), base, band_size - 1);
            _truthnqual = _insert(_right_shift<score_bytes>(_truthnqual), base == 'N' ? n_score : infinity, band_size - 1);
            const auto gap_idx = pos_in_range ? pos : truth_len - 1;
            update_gap_penalty<trace_bits>(_gap_open, gap_open, gap_idx);
            update_gap_penalty<trace_bits>(_gap_extend, gap_extend, gap_idx);
            _initmask  = _left_shift<score_bytes>(_initmask);
            _initmask2 = _left_shift<score_bytes>(_initmask2);
            _m2 = _min(_m2, _min(_i2, _d2));
            if (s / 2 >= target_len) {
                cur_score = _extract(_m2, s / 2 - target_len);
                if (cur_score < minscore) {
                    minscore = cur_score;
                    minscoreidx = s + 1;
                }
            }
            _m2 = _add(_m2, _min(_andnot(_cmpeq(_targetwin, _truthwin), _qualitieswin), _truthnqual));
            _d2 = _min(_add(_d1, _gap_extend), _add(_min(_m1, _i1), _gap_open)); // allow I->D
            _i2 = _insert(_add(_min(_add(_right_shift<score_bytes>(_i1), _gap_extend),
                                         _add(_right_shift<score_bytes>(_m1), _gap_open)), _nuc_prior), infinity, band_size - 1);
            _backpointers[s + 1] = _or(_or(_and(_three, _m2), _left_shift_words<2 * insert_label>(_and(_three, _i2))),
                                                _left_shift_words<2 * delete_label>(_and(_three, _d2)));
            // set state labels
            _m2 = _andnot(_three, _m2);
            _i2 = _or(_andnot(_three, _i2), _right_shift<1>(_three));
            _d2 = _or(_andnot(_three, _d2), _three);
        }
        if (minscoreidx < 0) {
            // minscore was never updated so we must have overflowed badly
            first_pos = -1;
            return -1;
        }
        s = minscoreidx;    // point to the dummy match transition
        auto i      = s / 2 - target_len;
        auto y      = target_len;
        auto x      = s - y;
        auto alnidx = 0;
        auto ptr = reinterpret_cast<short*>(_backpointers.data() + s);
        if ((ptr + i) < reinterpret_cast<short*>(_backpointers.data())
            || (ptr + i) >= reinterpret_cast<short*>(_backpointers.data() + _backpointers.size())) {
            first_pos = -1;
            return -1;
        }
        auto state = (ptr[i] >> (2 * match_label)) & 3;
        s -= 2;
        // this is 2*y (s even) or 2*y+1 (s odd)
        while (y > 0) {
            if (s < 0 || i < 0) {
                // This should never happen so must have overflowed
                first_pos = -1;
                return -1;
            }
            const auto new_state = (reinterpret_cast<short*>(_backpointers.data() + s)[i] >> (2 * state)) & 3;
            if (state == match_label) {
                s -= 2;
                align1[alnidx] = truth[--x];
                align2[alnidx] = target[--y];
            } else if (state == insert_label) {
                i += s & 1;
                s -= 1;
                align1[alnidx] = gap_label;
                align2[alnidx] = target[--y];
            } else {
                s -= 1;
                i -= s & 1;
                align1[alnidx] = truth[--x];
                align2[alnidx] = gap_label;
            }
            state = new_state;
            alnidx++;
        }
        align1[alnidx] = 0;
        align2[alnidx] = 0;
        first_pos = x;
        // reverse them
        for (int j {alnidx - 1}, i = 0; i < j; ++i, j--) {
            x = align1[i];
            y = align2[i];
            align1[i] = align1[j];
            align2[i] = align2[j];
            align1[j] = x;
            align2[j] = y;
        }
        return (minscore + bitmask) >> trace_bits;
    }
};

} // namespace simd
} // namespace hmm
} // namespace octopus

#endif
