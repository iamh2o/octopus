//
//  vcf_record.cpp
//  octopus
//
//  Created by Daniel Cooke on 28/07/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "vcf_record.hpp"

#include <algorithm>
#include <iterator>

namespace octopus {

// public methods

const std::string& VcfRecord::chrom() const noexcept
{
    return chrom_;
}

VcfRecord::SizeType VcfRecord::pos() const noexcept
{
    return pos_;
}

const VcfRecord::IdType& VcfRecord::id() const noexcept
{
    return id_;
}

const VcfRecord::NucleotideSequence& VcfRecord::ref() const noexcept
{
    return ref_;
}

unsigned VcfRecord::num_alt() const noexcept
{
    return static_cast<unsigned>(alt_.size());
}

const std::vector<VcfRecord::NucleotideSequence>& VcfRecord::alt() const noexcept
{
    return alt_;
}

boost::optional<VcfRecord::QualityType> VcfRecord::qual() const noexcept
{
    return qual_;
}

bool VcfRecord::has_filter(const KeyType& filter) const noexcept
{
    return std::find(std::cbegin(filter_), std::cend(filter_), filter) != std::cend(filter_);
}

const std::vector<VcfRecord::KeyType> VcfRecord::filter() const noexcept
{
    return filter_;
}

bool VcfRecord::has_info(const KeyType& key) const noexcept
{
    return info_.count(key) == 1;
}

std::vector<VcfRecord::KeyType> VcfRecord::info_keys() const
{
    std::vector<KeyType> result {};
    result.reserve(info_.size());
    
    std::transform(info_.cbegin(), info_.cend(), std::back_inserter(result), [] (const auto& p) {
        return p.first;
    });
    
    return result;
}

const std::vector<VcfRecord::ValueType>& VcfRecord::info_value(const KeyType& key) const
{
    return info_.at(key);
}

bool VcfRecord::has_format(const KeyType& key) const noexcept
{
    return std::find(std::cbegin(format_), std::cend(format_), key) != std::cend(format_);
}

unsigned VcfRecord::format_cardinality(const KeyType& key) const noexcept
{
    return (has_format(key)) ? static_cast<unsigned>(std::cbegin(samples_)->second.at(key).size()) : 0;
}

const std::vector<VcfRecord::KeyType>& VcfRecord::format() const noexcept
{
    return format_;
}

unsigned VcfRecord::num_samples() const noexcept
{
    return static_cast<unsigned>((has_genotypes()) ? genotypes_.size() : samples_.size());
}

bool VcfRecord::has_genotypes() const noexcept
{
    return !genotypes_.empty();
}

unsigned VcfRecord::ploidy(const SampleName& sample) const
{
    return static_cast<unsigned>(genotypes_.at(sample).first.size());
}

bool VcfRecord::is_sample_phased(const SampleName& sample) const
{
    return genotypes_.at(sample).second;
}

bool VcfRecord::is_homozygous(const SampleName& sample) const
{
    const auto& genotype = genotypes_.at(sample).first;
    return std::adjacent_find(std::cbegin(genotype), std::cend(genotype),
                              std::not_equal_to<NucleotideSequence>()) == std::cend(genotype);
}

bool VcfRecord::is_heterozygous(const SampleName& sample) const
{
    return !is_homozygous(sample);
}

bool VcfRecord::is_homozygous_ref(const SampleName& sample) const
{
    const auto& genotype = genotypes_.at(sample).first;
    return std::all_of(std::cbegin(genotype), std::cend(genotype),
                       [this] (const auto& allele) { return allele == ref_; });
}

bool VcfRecord::is_homozygous_non_ref(const SampleName& sample) const
{
    const auto& genotype = genotypes_.at(sample).first;
    return genotype.front() != ref_ && is_homozygous(sample);
}

bool VcfRecord::has_ref_allele(const SampleName& sample) const
{
    const auto& genotype = genotypes_.at(sample).first;
    return std::find(std::cbegin(genotype), std::cend(genotype), ref_) != std::cend(genotype);
}

bool VcfRecord::has_alt_allele(const SampleName& sample) const
{
    const auto& genotype = genotypes_.at(sample).first;
    return std::find_if_not(std::cbegin(genotype), std::cend(genotype),
                            [this] (const auto& allele) {
                                return allele == ref_;
                            }) != std::cend(genotype);
}

const std::vector<VcfRecord::ValueType>& VcfRecord::get_sample_value(const SampleName& sample, const KeyType& key) const
{
    return (key == "GT") ? genotypes_.at(sample).first : samples_.at(sample).at(key);
}

// helper non-members needed for printing

namespace
{
    template <typename T>
    std::ostream& print(std::ostream& os, const std::vector<T>& v, const std::string& delim = ",",
                        const std::string& empty_value = ".")
    {
        if (v.empty()) {
            os << empty_value;
        } else {
            std::copy(std::cbegin(v), std::prev(std::cend(v)),
                      std::ostream_iterator<T>(os, delim.c_str()));
            os << v.back();
        }
        return os;
    }
    
    template <typename T>
    std::ostream& operator<<(std::ostream& os, const std::vector<T>& v)
    {
        return print(os, v);
    }
} // namespace

// private methods

std::vector<VcfRecord::SampleName> VcfRecord::samples() const
{
    std::vector<SampleName> result {};
    
    if (has_genotypes()) {
        result.reserve(genotypes_.size());
        std::transform(std::cbegin(genotypes_), std::cend(genotypes_), std::back_inserter(result),
                       [] (const auto& p) { return p.first; });
    } else {
        result.reserve(samples_.size());
        std::transform(std::cbegin(samples_), std::cend(samples_), std::back_inserter(result),
                       [] (const auto& p) { return p.first; });
    }
    
    return result;
}

std::string VcfRecord::get_allele_number(const NucleotideSequence& allele) const
{
    if (allele == ".") {
        return ".";
    } else if (allele == ref_) {
        return "0";
    } else {
        const auto it = std::find(std::cbegin(alt_), std::cend(alt_), allele);
        return std::to_string(std::distance(std::cbegin(alt_), it) + 1);
    }
}

void VcfRecord::print_info(std::ostream& os) const
{
    if (info_.empty()) {
        os << ".";
    } else {
        auto last = std::next(std::cbegin(info_), info_.size() - 1);
        std::for_each(std::cbegin(info_), last,
                      [&os] (const auto& p) {
                          os << p.first;
                          if (!p.second.empty()) {
                              os << "=" << p.second;
                          }
                          os << ';';
                      });
        
        os << last->first;
        
        if (!last->second.empty()) {
            os << "=" << last->second;
        }
    }
}

void VcfRecord::print_genotype_allele_numbers(std::ostream& os, const SampleName& sample) const
{
    std::vector<std::string> allele_numbers(ploidy(sample));
    const auto& genotype = genotypes_.at(sample);
    std::transform(std::cbegin(genotype.first), std::cend(genotype.first), std::begin(allele_numbers),
                   [this] (const auto& allele) { return get_allele_number(allele); });
    print(os, allele_numbers, (genotype.second) ? "|" : "/");
}

void VcfRecord::print_other_sample_data(std::ostream& os, const SampleName& sample) const
{
    if (!samples_.empty()) {
        if (samples_.at(sample).empty()) {
            os << ".";
        } else {
            const auto& data = samples_.at(sample);
            auto last = std::next(cbegin(data), data.size() - 1);
            std::for_each(std::cbegin(data), last, [&os] (const auto& p) {
                print(os, p.second, ",");
                os << ":";
            });
            print(os, last->second, ",");
        }
    }
}

void VcfRecord::print_sample_data(std::ostream& os) const
{
    if (num_samples() > 0) {
        print(os, format_, ":");
        os << '\t';
        
        auto samples = this->samples();
        
        std::for_each(std::cbegin(samples), std::prev(std::cend(samples)),
                      [this, &os] (const auto& sample) {
                          auto it = std::cbegin(format_);
                          
                          if (*it == "GT") {
                              print_genotype_allele_numbers(os, sample);
                              ++it;
                          }
                          
                          std::for_each(it, std::cend(format_),
                                        [this, &os, &sample] (const auto& key) {
                                            os << ':';
                                            print(os, get_sample_value(sample, key), ",");
                                        });
                          
                          os << '\t';
        });
        
        auto it = std::cbegin(format_);
        
        if (*it == "GT") {
            print_genotype_allele_numbers(os, samples.back());
            ++it;
        }
        
        std::for_each(it, std::cend(format_),
                      [this, &os, &samples] (const auto& key) {
                          os << ':';
                          print(os, get_sample_value(samples.back(), key), ",");
                      });
    }
}

// non-member functions

// these INFO keys are reserved
static const VcfRecord::KeyType Info_ancestral_allele {"AA"};
static const VcfRecord::KeyType Info_genotype_allele_count {"AC"};
static const VcfRecord::KeyType Info_dbsnp {"DB"};
static const VcfRecord::KeyType Info_hapmap2 {"H2"};
static const VcfRecord::KeyType Info_hapmap3 {"H3"};
static const VcfRecord::KeyType Info_1000g {"1000G"};
static const VcfRecord::KeyType Info_somatic {"SOMATIC"};
static const VcfRecord::KeyType Info_validated {"VALIDATED"};

bool is_dbsnp_member(const VcfRecord& record) noexcept
{
    return record.has_info(Info_dbsnp);
}

bool is_hapmap2_member(const VcfRecord& record) noexcept
{
    return record.has_info(Info_hapmap2);
}

bool is_hapmap3_member(const VcfRecord& record) noexcept
{
    return record.has_info(Info_hapmap3);
}

bool is_1000g_member(const VcfRecord& record) noexcept
{
    return record.has_info(Info_1000g);
}

bool is_somatic(const VcfRecord& record) noexcept
{
    return record.has_info(Info_somatic);
}

bool is_validated(const VcfRecord& record) noexcept
{
    return record.has_info(Info_validated);
}

bool operator==(const VcfRecord& lhs, const VcfRecord& rhs)
{
    // TODO: this should really compare other fields
    return lhs.chrom() == rhs.chrom() && lhs.pos() == rhs.pos()
            && lhs.ref() == rhs.ref() && lhs.alt() == rhs.alt();
}

bool operator<(const VcfRecord& lhs, const VcfRecord& rhs)
{
    if (lhs.chrom() == rhs.chrom()) {
        if (lhs.pos() == rhs.pos()) {
            if (lhs.ref().size() == rhs.ref().size()) {
                return false; // TODO: also compare alt allele sizes
            } else {
                return lhs.ref().size() < rhs.ref().size();
            }
        } else {
            return lhs.pos() < rhs.pos();
        }
    } else {
        return lhs.chrom() < rhs.chrom();
    }
}

std::ostream& operator<<(std::ostream& os, const VcfRecord& record)
{
    os << record.chrom_ << "\t";
    os << (record.pos_ + 1) << "\t";
    os << record.id_ << "\t";
    os << record.ref_ << "\t";
    os << record.alt_ << "\t";
    if (record.qual_) {
        os << static_cast<float>(*record.qual_) << "\t";
    } else {
        os << '.' << "\t";
    }
    os << record.filter_ << "\t";
    record.print_info(os);
    os << "\t";
    record.print_sample_data(os);
    
    return os;
}

// VcfRecord::Builder

VcfRecord::Builder::Builder(const VcfRecord& call)
:
chrom_ {call.chrom()},
pos_ {call.pos()},
id_ {call.id()},
ref_ {call.ref()},
alt_ {call.alt()},
qual_ {call.qual()},
filter_ {call.filter()},
info_ {call.info_},
format_ {call.format()},
genotypes_ {call.genotypes_},
samples_ {call.samples_}
{}

VcfRecord::Builder& VcfRecord::Builder::set_chrom(std::string name)
{
    chrom_ = std::move(name);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_pos(SizeType pos)
{
    pos_ = pos;
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_id(IdType id)
{
    id_ = std::move(id);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_ref(const char allele)
{
    ref_ = allele;
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_ref(NucleotideSequence allele)
{
    ref_ = std::move(allele);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_alt(const char allele)
{
    alt_.resize(1);
    alt_.front() = allele;
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_alt(NucleotideSequence allele)
{
    alt_.resize(1);
    alt_.front() = std::move(allele);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_alt(std::vector<NucleotideSequence> alleles)
{
    alt_ = std::move(alleles);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_qual(QualityType quality)
{
    qual_ = quality;
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_passed()
{
    filter_.assign({"PASS"});
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_filter(std::vector<KeyType> filter)
{
    filter_ = std::move(filter);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_filter(std::initializer_list<KeyType> filter)
{
    filter_ = filter;
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::add_filter(KeyType filter)
{
    filter_.push_back(std::move(filter));
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::reserve_info(unsigned n)
{
    info_.reserve(n);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::add_info(const KeyType& key)
{
    info_.emplace(key, std::vector<ValueType> {});
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_info(const KeyType& key, const ValueType& value)
{
    return this->set_info(key, {value});
}

VcfRecord::Builder& VcfRecord::Builder::set_info(const KeyType& key, std::vector<ValueType> values)
{
    info_[key] = std::move(values);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_info(const KeyType& key, std::initializer_list<ValueType> values)
{
    return this->set_info(key, std::vector<ValueType> {values});
}

VcfRecord::Builder& VcfRecord::Builder::set_info_flag(KeyType key)
{
    return this->set_info(std::move(key), {});
}

VcfRecord::Builder& VcfRecord::Builder::clear_info() noexcept
{
    info_.clear();
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::clear_info(const KeyType& key)
{
    info_.erase(key);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_format(std::vector<KeyType> format)
{
    format_ = std::move(format);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_format(std::initializer_list<KeyType> format)
{
    format_ = std::move(format);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::add_format(KeyType key)
{
    format_.push_back(std::move(key));
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::reserve_samples(unsigned n)
{
    genotypes_.reserve(n);
    return *this;
}

VcfRecord::Builder&VcfRecord::Builder:: set_homozygous_ref_genotype(const SampleName& sample,
                                                                    unsigned ploidy)
{
    std::vector<NucleotideSequence> tmp(ploidy, ref_);
    return set_genotype(sample, tmp, Phasing::Phased);
}

VcfRecord::Builder& VcfRecord::Builder::set_genotype(const SampleName& sample,
                                                     std::vector<NucleotideSequence> alleles,
                                                     Phasing phasing)
{
    genotypes_[sample] = std::make_pair(std::move(alleles), phasing == Phasing::Phased);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_genotype(const SampleName& sample,
                                                     const std::vector<boost::optional<unsigned>>& alleles,
                                                     Phasing phasing)
{
    std::vector<NucleotideSequence> tmp {};
    tmp.reserve(alleles.size());
    
    std::transform(std::cbegin(alleles), std::cend(alleles), std::back_inserter(tmp),
                   [this] (const auto& allele) -> NucleotideSequence {
                       if (allele) {
                           return (*allele == 0) ? ref_ : alt_[*allele - 1];
                       } else {
                           return ".";
                       }
                   });
    
    return set_genotype(sample, tmp, phasing);
}

VcfRecord::Builder& VcfRecord::Builder::set_format(const SampleName& sample,
                                                   const KeyType& key,
                                                   const ValueType& value)
{
    return this->set_format(sample, key, std::vector<ValueType> {value});
}

VcfRecord::Builder& VcfRecord::Builder::set_format(const SampleName& sample,
                                                   const KeyType& key,
                                                   std::vector<ValueType> values)
{
    samples_[sample][key] = std::move(values);
    return *this;
}

VcfRecord::Builder& VcfRecord::Builder::set_format(const SampleName& sample,
                                                   const KeyType& key,
                                                   std::initializer_list<ValueType> values)
{
    return this->set_format(sample, key, std::vector<ValueType> {values});
}

VcfRecord::Builder& VcfRecord::Builder::set_format_missing(const SampleName& sample,
                                                           const KeyType& key)
{
    return this->set_format(sample, key, std::string {"."});
}

VcfRecord::Builder& VcfRecord::Builder::set_refcall()
{
    return set_alt("<NON_REF>");
}

VcfRecord::Builder& VcfRecord::Builder::set_somatic()
{
    return this->set_info_flag("SOMATIC");
}

VcfRecord::SizeType VcfRecord::Builder::pos() const noexcept
{
    return pos_;
}

VcfRecord VcfRecord::Builder::build() const
{
    if (genotypes_.empty() && samples_.empty()) {
        return VcfRecord {
            chrom_, pos_, id_, ref_, alt_, qual_, filter_, info_
        };
    } else {
        return VcfRecord {
            chrom_, pos_, id_, ref_, alt_, qual_, filter_, info_, format_, genotypes_, samples_
        };
    }
}

VcfRecord VcfRecord::Builder::build_once() noexcept
{
    if (genotypes_.empty() && samples_.empty()) {
        return VcfRecord {std::move(chrom_), pos_, std::move(id_), std::move(ref_),
            std::move(alt_), qual_, std::move(filter_), std::move(info_)};
    } else {
        return VcfRecord {std::move(chrom_), pos_, std::move(id_), std::move(ref_),
            std::move(alt_), qual_, std::move(filter_), std::move(info_),
            std::move(format_), std::move(genotypes_), std::move(samples_)};
    }
}

} // namespace octopus