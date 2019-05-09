// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef aligned_read_hpp
#define aligned_read_hpp

#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <bitset>
#include <algorithm>
#include <iterator>
#include <utility>
#include <functional>
#include <numeric>
#include <iosfwd>

#include <boost/optional.hpp>

#include "concepts/comparable.hpp"
#include "concepts/equitable.hpp"
#include "basics/genomic_region.hpp"
#include "concepts/mappable.hpp"
#include "utils/memory_footprint.hpp"
#include "cigar_string.hpp"

namespace octopus {

class AlignedRead : public Comparable<AlignedRead>, public Mappable<AlignedRead>
{
public:
    using MappingDomain       = GenomicRegion;
    using NucleotideSequence  = std::string;
    using MappingQuality      = std::uint8_t;
    using BaseQuality         = std::uint8_t;
    using BaseQualityVector   = std::vector<BaseQuality>;
    
    enum class Direction { forward, reverse };
    
    class Segment : public Equitable<Segment>
    {
    public:
        struct Flags;
        
        Segment() = default;
        
        template <typename S>
        Segment(S&& contig_name, GenomicRegion::Position begin,
                GenomicRegion::Size inferred_template_length,
                Flags data);
        
        Segment(const Segment&)            = default;
        Segment& operator=(const Segment&) = default;
        Segment(Segment&&)                 = default;
        Segment& operator=(Segment&&)      = default;
        
        ~Segment() = default;
        
        const GenomicRegion::ContigName& contig_name() const;
        
        GenomicRegion::Position begin() const noexcept;
        
        GenomicRegion::Size inferred_template_length() const noexcept;
        
        bool is_marked_unmapped() const;
        bool is_marked_reverse_mapped() const;
        
        std::size_t get_hash() const;
        
        friend bool operator==(const AlignedRead::Segment& lhs, const AlignedRead::Segment& rhs) noexcept;
        
    private:
        using FlagBits = std::bitset<2>;
        
        GenomicRegion::ContigName contig_name_;
        GenomicRegion::Position begin_;
        GenomicRegion::Size inferred_template_length_;
        FlagBits flags_;
        
        FlagBits compress(const Flags& data);
    };
    
    struct Flags;
    
    AlignedRead() = default;
    
    template <typename String1_, typename GenomicRegion_, typename Seq1_, typename Seq2_,
              typename Qualities_, typename CigarString_, typename String2_>
    AlignedRead(String1_&& name, GenomicRegion_&& reference_region, Seq1_&& sequence, Qualities_&& qualities,
                CigarString_&& cigar, MappingQuality mapping_quality, const Flags& flags, Seq2_&& barcode,
                String2_&& read_group);
    template <typename String1_, typename GenomicRegion_, typename Seq1_, typename Seq2_,
              typename Qualities_, typename CigarString_, typename String2_, typename String3_>
    AlignedRead(String1_&& name, GenomicRegion_&& reference_region, Seq1_&& sequence, Qualities_&& qualities,
                CigarString_&& cigar, MappingQuality mapping_quality, Flags flags, String2_&& read_group, Seq2_&& barcode,
                String3_&& next_segment_contig_name, MappingDomain::Position next_segment_begin,
                MappingDomain::Size inferred_template_length,
                const Segment::Flags& next_segment_flags);
    
    AlignedRead(const AlignedRead& other)            = default;
    AlignedRead& operator=(const AlignedRead& other) = default;
    AlignedRead(AlignedRead&&)                       = default;
    AlignedRead& operator=(AlignedRead&&)            = default;
    
    ~AlignedRead() = default;
    
    const std::string& name() const noexcept;
    const std::string& read_group() const noexcept;
    const GenomicRegion& mapped_region() const noexcept;
    const NucleotideSequence& sequence() const noexcept;
    NucleotideSequence& sequence() noexcept;
    const BaseQualityVector& base_qualities() const noexcept;
    BaseQualityVector& base_qualities() noexcept;
    MappingQuality mapping_quality() const noexcept;
    const CigarString& cigar() const noexcept;
    Direction direction() const noexcept;
    bool has_other_segment() const noexcept;
    const Segment& next_segment() const;
    Flags flags() const noexcept;
    const NucleotideSequence& barcode() const noexcept;
    
    void realign(GenomicRegion new_region, CigarString new_cigar) noexcept;
    
    bool is_marked_all_segments_in_read_aligned() const noexcept;
    bool is_marked_multiple_segment_template() const noexcept;
    bool is_marked_unmapped() const noexcept;
    bool is_marked_next_segment_unmapped() const noexcept;
    bool is_marked_reverse_mapped() const noexcept;
    bool is_marked_next_segment_reverse_mapped() const noexcept;
    bool is_marked_secondary_alignment() const noexcept;
    bool is_marked_qc_fail() const noexcept;
    bool is_marked_duplicate() const noexcept;
    bool is_marked_supplementary_alignment() const noexcept;
    bool is_marked_first_template_segment() const noexcept;
    bool is_marked_last_template_segment() const noexcept;
    
    friend bool operator==(const AlignedRead& lhs, const AlignedRead& rhs) noexcept;
    friend bool operator<(const AlignedRead& lhs, const AlignedRead& rhs) noexcept;
    
private:
    static constexpr std::size_t numFlags_ = 10;
    using FlagBits = std::bitset<numFlags_>;
    
    // should be ordered by sizeof
    GenomicRegion region_;
    std::string name_;
    NucleotideSequence sequence_, barcode_sequence_;
    BaseQualityVector base_qualities_;
    CigarString cigar_;
    std::string read_group_;
    boost::optional<Segment> next_segment_;
    FlagBits flags_;
    MappingQuality mapping_quality_;
    
    FlagBits compress(const Flags& flags) const noexcept;
    Flags decompress(const FlagBits& flags) const noexcept;
};

struct AlignedRead::Segment::Flags
{
    bool unmapped;
    bool reverse_mapped;
};

struct AlignedRead::Flags
{
    bool multiple_segment_template;
    bool all_segments_in_read_aligned;
    bool unmapped;
    bool reverse_mapped;
    bool secondary_alignment;
    bool qc_fail;
    bool duplicate;
    bool supplementary_alignment;
    bool first_template_segment;
    bool last_template_segment;
};

template <typename String_, typename GenomicRegion_, typename Seq1, typename Seq2,
          typename Qualities_, typename CigarString_, typename String2_>
AlignedRead::AlignedRead(String_&& name, GenomicRegion_&& reference_region, Seq1&& sequence, Qualities_&& qualities,
                         CigarString_&& cigar, MappingQuality mapping_quality, const Flags& flags, Seq2&& barcode,
                         String2_&& read_group)
: region_ {std::forward<GenomicRegion_>(reference_region)}
, name_ {std::forward<String_>(name)}
, sequence_ {std::forward<Seq1>(sequence)}
, barcode_sequence_ {std::forward<Seq2>(barcode)}
, base_qualities_ {std::forward<Qualities_>(qualities)}
, cigar_ {std::forward<CigarString_>(cigar)}
, read_group_ {std::forward<String2_>(read_group)}
, next_segment_ {}
, flags_ {compress(flags)}
, mapping_quality_ {mapping_quality}
{}

template <typename String1_, typename GenomicRegion_, typename Seq1, typename Seq2, typename Qualities_,
          typename CigarString_, typename String2_, typename String3_>
AlignedRead::AlignedRead(String1_&& name, GenomicRegion_&& reference_region, Seq1&& sequence, Qualities_&& qualities,
                         CigarString_&& cigar, MappingQuality mapping_quality, Flags flags, String2_&& read_group,
                         Seq2&& barcode,
                         String3_&& next_segment_contig_name, MappingDomain::Position next_segment_begin,
                         MappingDomain::Size inferred_template_length, const Segment::Flags& next_segment_flags)
: region_ {std::forward<GenomicRegion_>(reference_region)}
, name_ {std::forward<String1_>(name)}
, sequence_ {std::forward<Seq1>(sequence)}
, barcode_sequence_ {std::forward<Seq2>(barcode)}
, base_qualities_ {std::forward<Qualities_>(qualities)}
, cigar_ {std::forward<CigarString_>(cigar)}
, read_group_ {std::forward<String2_>(read_group)}
, next_segment_ {
    Segment {std::forward<String3_>(next_segment_contig_name), next_segment_begin,
    inferred_template_length, next_segment_flags}
  }
, flags_ {compress(flags)}
, mapping_quality_ {mapping_quality}
{}

template <typename String_>
AlignedRead::Segment::Segment(String_&& contig_name, GenomicRegion::Position begin,
                              GenomicRegion::Size inferred_template_length, Flags data)
: contig_name_ {std::forward<String_>(contig_name)}
, begin_ {begin}
, inferred_template_length_ {inferred_template_length}
, flags_ {compress(data)}
{}

// Non-member methods

void capitalise_bases(AlignedRead& read) noexcept;

void cap_qualities(AlignedRead& read, AlignedRead::BaseQuality max = 0) noexcept;

void set_front_qualities(AlignedRead& read, std::size_t num_bases, AlignedRead::BaseQuality value) noexcept;
void zero_front_qualities(AlignedRead& read, std::size_t num_bases) noexcept;
void set_back_qualities(AlignedRead& read, std::size_t num_bases, AlignedRead::BaseQuality value) noexcept;
void zero_back_qualities(AlignedRead& read, std::size_t num_bases) noexcept;

bool is_sequence_empty(const AlignedRead& read) noexcept;

AlignedRead::NucleotideSequence::size_type sequence_size(const AlignedRead& read) noexcept;
AlignedRead::NucleotideSequence::size_type sequence_size(const AlignedRead& read, const GenomicRegion& region);

bool is_forward_strand(const AlignedRead& read) noexcept;
bool is_reverse_strand(const AlignedRead& read) noexcept;

bool is_primary_alignment(const AlignedRead& read) noexcept;

bool is_soft_clipped(const AlignedRead& read) noexcept;
bool is_front_soft_clipped(const AlignedRead& read) noexcept;
bool is_back_soft_clipped(const AlignedRead& read) noexcept;
std::pair<CigarOperation::Size, CigarOperation::Size> get_soft_clipped_sizes(const AlignedRead& read) noexcept;
CigarOperation::Size total_clip_size(const AlignedRead& read) noexcept;
GenomicRegion clipped_mapped_region(const AlignedRead& read);
bool has_indel(const AlignedRead& read) noexcept;
int sum_indel_sizes(const AlignedRead& read) noexcept;
int max_indel_size(const AlignedRead& read) noexcept;

// Returns the part of the read cigar string contained by the region
CigarString copy_cigar(const AlignedRead& read, const GenomicRegion& region);
// Returns the part of the read (cigar, sequence, base_qualities) contained by the region
AlignedRead copy(const AlignedRead& read, const GenomicRegion& region);
AlignedRead::NucleotideSequence copy_sequence(const AlignedRead& read, const GenomicRegion& region);
AlignedRead::BaseQualityVector copy_base_qualities(const AlignedRead& read, const GenomicRegion& region);

MemoryFootprint footprint(const AlignedRead& read) noexcept;

template <typename Range>
MemoryFootprint footprint(const Range& reads) noexcept
{
    return std::accumulate(std::cbegin(reads), std::cend(reads), MemoryFootprint {0},
                           [] (auto curr, const auto& read) noexcept { return curr + footprint(read); });
}

bool operator==(const AlignedRead& lhs, const AlignedRead& rhs) noexcept;
bool operator<(const AlignedRead& lhs, const AlignedRead& rhs) noexcept;

bool operator==(const AlignedRead::Segment& lhs, const AlignedRead::Segment& rhs) noexcept;

bool operator==(const AlignedRead::Flags& lhs, const AlignedRead::Flags& rhs) noexcept;

std::ostream& operator<<(std::ostream& os, const AlignedRead::BaseQualityVector& qualities);
std::ostream& operator<<(std::ostream& os, const AlignedRead& read);

struct ReadHash
{
    std::size_t operator()(const AlignedRead& read) const;
};

} // namespace octopus

namespace std {

template <> struct hash<octopus::AlignedRead>
{
    size_t operator()(const octopus::AlignedRead& read) const
    {
        return octopus::ReadHash()(read);
    }
};

template <> struct hash<reference_wrapper<const octopus::AlignedRead>>
{
    size_t operator()(const reference_wrapper<const octopus::AlignedRead> read) const
    {
        return hash<octopus::AlignedRead>()(read);
    }
};

} // namespace std

namespace boost {

template <> struct hash<octopus::AlignedRead> : std::unary_function<octopus::AlignedRead, std::size_t>
{
    std::size_t operator()(const octopus::AlignedRead& read) const
    {
        return std::hash<octopus::AlignedRead>()(read);
    }
};

} // namespace boost

#endif
