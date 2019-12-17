// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "polyclone_caller.hpp"

#include <typeinfo>
#include <unordered_map>
#include <deque>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <utility>
#include <stdexcept>
#include <iostream>
#include <limits>

#include "basics/genomic_region.hpp"
#include "containers/probability_matrix.hpp"
#include "core/types/allele.hpp"
#include "core/types/variant.hpp"
#include "core/types/calls/germline_variant_call.hpp"
#include "core/types/calls/reference_call.hpp"
#include "core/models/genotype/uniform_genotype_prior_model.hpp"
#include "core/models/genotype/coalescent_genotype_prior_model.hpp"
#include "utils/mappable_algorithms.hpp"
#include "utils/read_stats.hpp"
#include "utils/concat.hpp"
#include "logging/logging.hpp"

namespace octopus {

PolycloneCaller::PolycloneCaller(Caller::Components&& components,
                                 Caller::Parameters general_parameters,
                                 Parameters specific_parameters)
: Caller {std::move(components), std::move(general_parameters)}
, parameters_ {std::move(specific_parameters)}
{
    if (parameters_.max_clones < 1) {
        throw std::logic_error {"PolycloneCaller: max_clones must be > 1"};
    }
    if (parameters_.max_clones > model::SubcloneModel::max_ploidy) {
        static std::atomic_bool warned {false};
        if (!warned) {
            warned = true;
            logging::WarningLogger log {};
            stream(log) << "Maximum supported clonality is "
                            << model::SubcloneModel::max_ploidy
                            << " but " << parameters_.max_clones << " was requested";
        }
        parameters_.max_clones = model::SubcloneModel::max_ploidy;
    }
}

std::string PolycloneCaller::do_name() const
{
    return "polyclone";
}

PolycloneCaller::CallTypeSet PolycloneCaller::do_call_types() const
{
    return {std::type_index(typeid(GermlineVariantCall))};
}

unsigned PolycloneCaller::do_min_callable_ploidy() const
{
    return 1;
}

unsigned PolycloneCaller::do_max_callable_ploidy() const
{
    return parameters_.max_clones;
}

std::size_t PolycloneCaller::do_remove_duplicates(HaplotypeBlock& haplotypes) const
{
    if (parameters_.deduplicate_haplotypes_with_germline_model) {
        if (haplotypes.size() < 2) return 0;
        CoalescentModel::Parameters model_params {};
        if (parameters_.prior_model_params) model_params = *parameters_.prior_model_params;
        Haplotype reference {mapped_region(haplotypes), reference_.get()};
        CoalescentModel model {std::move(reference), model_params, haplotypes.size(), CoalescentModel::CachingStrategy::none};
        const CoalescentProbabilityGreater cmp {std::move(model)};
        return octopus::remove_duplicates(haplotypes, cmp);
    } else {
        return Caller::do_remove_duplicates(haplotypes);
    }
}

// PolycloneCaller::Latents public methods

PolycloneCaller::Latents::Latents(std::vector<Genotype<Haplotype>> haploid_genotypes, std::vector<Genotype<Haplotype>> polyploid_genotypes,
                                   HaploidModelInferences haploid_model_inferences, SubloneModelInferences subclone_model_inferences,
                                   const SampleName& sample, const std::function<double(unsigned)>& clonality_prior)
: haploid_genotypes_ {std::move(haploid_genotypes)}
, polyploid_genotypes_ {std::move(polyploid_genotypes)}
, haploid_model_inferences_ {std::move(haploid_model_inferences)}
, subclone_model_inferences_ {std::move(subclone_model_inferences)}
, model_log_posteriors_ {0, std::numeric_limits<double>::min()}
, sample_ {sample}
{
    if (!polyploid_genotypes_.empty()) {
        const auto haploid_model_prior = std::log(clonality_prior(1));
        const auto called_subclonality = polyploid_genotypes_.front().ploidy();
        const auto subclone_model_prior = std::log(clonality_prior(called_subclonality));
        const auto haploid_model_jp = haploid_model_prior + haploid_model_inferences_.log_evidence;
        const auto subclone_model_jp = subclone_model_prior + subclone_model_inferences_.approx_log_evidence;
        const auto norm = maths::log_sum_exp({haploid_model_jp, subclone_model_jp});
        model_log_posteriors_ = {haploid_model_jp - norm, subclone_model_jp - norm};
        auto log_posteriors = concat(haploid_model_inferences_.posteriors.genotype_log_probabilities,
                                     subclone_model_inferences_.max_evidence_params.genotype_log_probabilities);
        std::for_each(std::begin(log_posteriors), std::next(std::begin(log_posteriors), haploid_genotypes_.size()),
                      [&] (auto& p) { p += model_log_posteriors_.clonal; });
        std::for_each(std::next(std::begin(log_posteriors), haploid_genotypes_.size()), std::end(log_posteriors),
                      [&] (auto& p) { p += model_log_posteriors_.subclonal; });
        auto genotypes = concat(haploid_genotypes_, polyploid_genotypes_);
        genotype_log_posteriors_ = std::make_shared<GenotypeProbabilityMap>(std::make_move_iterator(std::begin(genotypes)),
                                                                            std::make_move_iterator(std::end(genotypes)));
        insert_sample(sample_, log_posteriors, *genotype_log_posteriors_);
    }
}

std::shared_ptr<PolycloneCaller::Latents::HaplotypeProbabilityMap>
PolycloneCaller::Latents::haplotype_posteriors() const noexcept
{
    if (haplotype_posteriors_ == nullptr) {
        haplotype_posteriors_ = std::make_shared<HaplotypeProbabilityMap>();
        for (const auto& p : (*(this->genotype_posteriors()))[sample_]) {
            for (const auto& haplotype : p.first.copy_unique_ref()) {
                (*haplotype_posteriors_)[haplotype] += p.second;
            }
        }
    }
    return haplotype_posteriors_;
}

std::shared_ptr<PolycloneCaller::Latents::GenotypeProbabilityMap>
PolycloneCaller::Latents::genotype_posteriors() const noexcept
{
    if (genotype_posteriors_ == nullptr) {
        auto genotypes = concat(haploid_genotypes_, polyploid_genotypes_);
        auto posteriors = concat(haploid_model_inferences_.posteriors.genotype_probabilities,
                                 subclone_model_inferences_.max_evidence_params.genotype_probabilities);
        const ModelProbabilities model_posterior {std::exp( model_log_posteriors_.clonal), std::exp( model_log_posteriors_.subclonal)};
        std::for_each(std::begin(posteriors), std::next(std::begin(posteriors), haploid_genotypes_.size()),
                      [&] (auto& p) { p *= model_posterior.clonal; });
        std::for_each(std::next(std::begin(posteriors), haploid_genotypes_.size()), std::end(posteriors),
                      [=] (auto& p) { p *= model_posterior.subclonal; });
        genotype_posteriors_ = std::make_shared<GenotypeProbabilityMap>(std::make_move_iterator(std::begin(genotypes)),
                                                                        std::make_move_iterator(std::end(genotypes)));
        insert_sample(sample_, posteriors, *genotype_posteriors_);
    }
    return genotype_posteriors_;
}

// PolycloneCaller::Latents private methods

std::unique_ptr<PolycloneCaller::Caller::Latents>
PolycloneCaller::infer_latents(const HaplotypeBlock& haplotypes, const HaplotypeLikelihoodArray& haplotype_likelihoods) const
{
    auto haploid_genotypes = generate_all_genotypes(haplotypes, 1);
    if (debug_log_) stream(*debug_log_) << "There are " << haploid_genotypes.size() << " candidate haploid genotypes";
    auto genotype_prior_model = make_prior_model(haplotypes);
    const model::IndividualModel haploid_model {*genotype_prior_model, debug_log_};
    haplotype_likelihoods.prime(sample());
    auto haploid_inferences = haploid_model.evaluate(haploid_genotypes, haplotype_likelihoods);
    if (debug_log_) stream(*debug_log_) << "Evidence for haploid model is " << haploid_inferences.log_evidence;
    IndexedGenotypeVectorPair polyploid_genotypes {};
    model::SubcloneModel::InferredLatents sublonal_inferences;
    fit_sublone_model(haplotypes, haplotype_likelihoods, *genotype_prior_model, haploid_inferences.log_evidence,
                      polyploid_genotypes,sublonal_inferences);
    if (debug_log_) stream(*debug_log_) << "There are " << polyploid_genotypes.raw.size() << " candidate polyploid genotypes";
    using std::move;
    return std::make_unique<Latents>(move(haploid_genotypes), move(polyploid_genotypes.raw),
                                     move(haploid_inferences), move(sublonal_inferences),
                                     sample(), parameters_.clonality_prior);
}

boost::optional<double>
PolycloneCaller::calculate_model_posterior(const HaplotypeBlock& haplotypes,
                                           const HaplotypeLikelihoodArray& haplotype_likelihoods,
                                           const Caller::Latents& latents) const
{
    return calculate_model_posterior(haplotypes, haplotype_likelihoods, dynamic_cast<const Latents&>(latents));
}

boost::optional<double>
PolycloneCaller::calculate_model_posterior(const HaplotypeBlock& haplotypes,
                                           const HaplotypeLikelihoodArray& haplotype_likelihoods,
                                           const Latents& latents) const
{
    return boost::none;
}

std::vector<std::unique_ptr<octopus::VariantCall>>
PolycloneCaller::call_variants(const std::vector<Variant>& candidates, const Caller::Latents& latents) const
{
    return call_variants(candidates, dynamic_cast<const Latents&>(latents));
}

namespace {

using GenotypeProbabilityMap = ProbabilityMatrix<Genotype<Haplotype>>::InnerMap;
using VariantReference = std::reference_wrapper<const Variant>;
using VariantPosteriorVector = std::vector<std::pair<VariantReference, Phred<double>>>;

struct VariantCall : Mappable<VariantCall>
{
    VariantCall() = delete;
    VariantCall(const std::pair<VariantReference, Phred<double>>& p)
    : variant {p.first}
    , posterior {p.second}
    {}
    VariantCall(const Variant& variant, Phred<double> posterior)
    : variant {variant}
    , posterior {posterior}
    {}
    
    const GenomicRegion& mapped_region() const noexcept
    {
        return octopus::mapped_region(variant.get());
    }
    
    VariantReference variant;
    Phred<double> posterior;
    bool is_dummy_filtered = false;
};

using VariantCalls = std::vector<VariantCall>;

struct GenotypeCall
{
    template <typename T> GenotypeCall(T&& genotype, Phred<double> posterior)
    : genotype {std::forward<T>(genotype)}
    , posterior {posterior}
    {}
    
    Genotype<Allele> genotype;
    Phred<double> posterior;
};

using GenotypeCalls = std::vector<GenotypeCall>;

// allele posterior calculations

template <typename GenotypeOrAllele>
auto marginalise_contained(const GenotypeOrAllele& element, const GenotypeProbabilityMap& genotype_log_posteriors)
{
    thread_local std::vector<double> buffer {};
    buffer.clear();
    for (const auto& p : genotype_log_posteriors) {
        if (!contains(p.first, element)) {
            buffer.push_back(p.second);
        }
    }
    if (!buffer.empty()) {
        return log_probability_false_to_phred(std::min(maths::log_sum_exp(buffer), 0.0));
    } else {
        return Phred<> {std::numeric_limits<double>::infinity()};
    }
}

auto compute_candidate_posteriors(const std::vector<Variant>& candidates,
                                  const GenotypeProbabilityMap& genotype_log_posteriors)
{
    VariantPosteriorVector result {};
    result.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        result.emplace_back(candidate, marginalise_contained(candidate.alt_allele(), genotype_log_posteriors));
    }
    return result;
}

// variant calling

bool has_callable(const VariantPosteriorVector& variant_posteriors, const Phred<double> min_posterior) noexcept
{
    return std::any_of(std::cbegin(variant_posteriors), std::cend(variant_posteriors),
                       [=] (const auto& p) noexcept { return p.second >= min_posterior; });
}

bool contains_alt(const Genotype<Haplotype>& genotype_call, const VariantReference& candidate)
{
    return includes(genotype_call, candidate.get().alt_allele());
}

VariantCalls call_candidates(const VariantPosteriorVector& candidate_posteriors,
                             const Genotype<Haplotype>& genotype_call,
                             const Phred<double> min_posterior)
{
    VariantCalls result {};
    result.reserve(candidate_posteriors.size());
    std::copy_if(std::cbegin(candidate_posteriors), std::cend(candidate_posteriors), std::back_inserter(result),
                 [&genotype_call, min_posterior] (const auto& p) {
                     return p.second >= min_posterior && contains_alt(genotype_call, p.first);
                 });
    return result;
}

// variant genotype calling

template <typename PairIterator>
PairIterator find_map(PairIterator first, PairIterator last)
{
    return std::max_element(first, last, [] (const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
}

template <typename T>
bool is_homozygous_reference(const Genotype<T>& g)
{
    return is_reference(g[0]) && g.is_homozygous();
}

auto call_genotype(const GenotypeProbabilityMap& genotype_posteriors, const bool ignore_hom_ref = false)
{
    const auto map_itr = find_map(std::cbegin(genotype_posteriors), std::cend(genotype_posteriors));
    assert(map_itr != std::cend(genotype_posteriors));
    if (!ignore_hom_ref || !is_homozygous_reference(map_itr->first)) {
        return map_itr->first;
    } else {
        const auto lhs_map_itr = find_map(std::cbegin(genotype_posteriors), map_itr);
        const auto rhs_map_itr = find_map(std::next(map_itr), std::cend(genotype_posteriors));
        if (lhs_map_itr != map_itr) {
            if (rhs_map_itr != std::cend(genotype_posteriors)) {
                return lhs_map_itr->second < rhs_map_itr->second ? rhs_map_itr->first : lhs_map_itr->first;
            } else {
                return lhs_map_itr->first;
            }
        } else {
            return rhs_map_itr->first;
        }
    }
}

auto compute_posterior(const Genotype<Allele>& genotype, const GenotypeProbabilityMap& genotype_posteriors)
{
    auto p = std::accumulate(std::cbegin(genotype_posteriors), std::cend(genotype_posteriors), 0.0,
                             [&genotype] (const double curr, const auto& p) {
                                 return curr + (contains(p.first, genotype) ? 0.0 : p.second);
                             });
    return probability_false_to_phred(p);
}

GenotypeCalls call_genotypes(const Genotype<Haplotype>& genotype_call,
                             const GenotypeProbabilityMap& genotype_log_posteriors,
                             const std::vector<GenomicRegion>& variant_regions)
{
    GenotypeCalls result {};
    result.reserve(variant_regions.size());
    for (const auto& region : variant_regions) {
        auto genotype_chunk = copy<Allele>(genotype_call, region);
        const auto posterior = marginalise_contained(genotype_chunk, genotype_log_posteriors);
        result.emplace_back(std::move(genotype_chunk), posterior);
    }
    return result;
}

// output

octopus::VariantCall::GenotypeCall convert(GenotypeCall&& call)
{
    return octopus::VariantCall::GenotypeCall {std::move(call.genotype), call.posterior};
}

std::unique_ptr<octopus::VariantCall>
transform_call(const SampleName& sample, VariantCall&& variant_call, GenotypeCall&& genotype_call)
{
    std::vector<std::pair<SampleName, Call::GenotypeCall>> tmp {std::make_pair(sample, convert(std::move(genotype_call)))};
    std::unique_ptr<octopus::VariantCall> result {std::make_unique<GermlineVariantCall>(variant_call.variant.get(), std::move(tmp),
                                                                                        variant_call.posterior)};
    return result;
}

auto transform_calls(const SampleName& sample, VariantCalls&& variant_calls, GenotypeCalls&& genotype_calls)
{
    std::vector<std::unique_ptr<octopus::VariantCall>> result {};
    result.reserve(variant_calls.size());
    std::transform(std::make_move_iterator(std::begin(variant_calls)), std::make_move_iterator(std::end(variant_calls)),
                   std::make_move_iterator(std::begin(genotype_calls)), std::back_inserter(result),
                   [&sample] (VariantCall&& variant_call, GenotypeCall&& genotype_call) {
                       return transform_call(sample, std::move(variant_call), std::move(genotype_call));
                   });
    return result;
}

} // namespace

namespace debug { namespace {

void log(const GenotypeProbabilityMap& genotype_posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log);

void log(const VariantPosteriorVector& candidate_posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log,
         Phred<double> min_posterior);

void log(const Genotype<Haplotype>& called_genotype,
         boost::optional<logging::DebugLogger>& debug_log);

} // namespace
} // namespace debug

std::vector<std::unique_ptr<octopus::VariantCall>>
PolycloneCaller::call_variants(const std::vector<Variant>& candidates, const Latents& latents) const
{
    log(latents);
    const auto& genotype_log_posteriors = (*latents.genotype_log_posteriors_)[sample()];
    debug::log(genotype_log_posteriors, debug_log_, trace_log_);
    const auto candidate_posteriors = compute_candidate_posteriors(candidates, genotype_log_posteriors);
    debug::log(candidate_posteriors, debug_log_, trace_log_, parameters_.min_variant_posterior);
    const bool force_call_non_ref {has_callable(candidate_posteriors, parameters_.min_variant_posterior)};
    const auto genotype_call = octopus::call_genotype(genotype_log_posteriors, force_call_non_ref);
    auto variant_calls = call_candidates(candidate_posteriors, genotype_call, parameters_.min_variant_posterior);
    const auto called_regions = extract_regions(variant_calls);
    auto genotype_calls = call_genotypes(genotype_call, genotype_log_posteriors, called_regions);
    return transform_calls(sample(), std::move(variant_calls), std::move(genotype_calls));
}

std::vector<std::unique_ptr<ReferenceCall>>
PolycloneCaller::call_reference(const std::vector<Allele>& alleles, const Caller::Latents& latents, const ReadPileupMap& pileup) const
{
    return call_reference(alleles, dynamic_cast<const Latents&>(latents), pileup);
}

std::vector<std::unique_ptr<ReferenceCall>>
PolycloneCaller::call_reference(const std::vector<Allele>& alleles, const Latents& latents, const ReadPileupMap& pileup) const
{
    return {};
}

const SampleName& PolycloneCaller::sample() const noexcept
{
    return samples_.front();
}

namespace {

auto make_sublone_model_mixture_prior_map(const SampleName& sample, const unsigned num_clones, const double alpha = 1.0)
{
    model::SubcloneModel::Priors::GenotypeMixturesDirichletAlphaMap result {};
    model::SubcloneModel::Priors::GenotypeMixturesDirichletAlphas alphas(num_clones, alpha);
    result.emplace(sample, std::move(alphas));
    return result;
}

template <typename T>
T nth_greatest_value(std::vector<T> values, const std::size_t n)
{
    auto nth_itr = std::next(std::begin(values), n);
    std::nth_element(std::begin(values), nth_itr, std::end(values), std::greater<> {});
    return *nth_itr;
}

template <typename T>
void erase_indices(std::vector<T>& v, const std::vector<std::size_t>& indices)
{
    assert(std::is_sorted(std::cbegin(indices), std::cend(indices)));
    std::for_each(std::crbegin(indices), std::crend(indices), [&v] (auto idx) { v.erase(std::next(std::cbegin(v), idx)); });
}

template <typename IndexedGenotypeVectorPair>
void reduce(IndexedGenotypeVectorPair& genotypes,
            const std::vector<double>& genotype_probabilities,
            const std::size_t n)
{
    if (genotypes.raw.size() <= n) return;
    const auto min_probability = nth_greatest_value(genotype_probabilities, n + 1);
    std::size_t idx {0};
    const auto is_low_probability = [&] (const auto& genotype) { return genotype_probabilities[idx++] <= min_probability; };
    genotypes.raw.erase(std::remove_if(std::begin(genotypes.raw), std::end(genotypes.raw),is_low_probability), std::end(genotypes.raw));
    idx = 0;
    genotypes.indices.erase(std::remove_if(std::begin(genotypes.indices), std::end(genotypes.indices), is_low_probability), std::end(genotypes.indices));
}

template <typename IndexedGenotypeVectorPair>
void reduce(IndexedGenotypeVectorPair& genotypes,
            const MappableBlock<Haplotype>& haplotypes,
            const GenotypePriorModel& genotype_prior_model,
            const HaplotypeLikelihoodArray& haplotype_likelihoods,
            const std::size_t n)
{
    if (genotypes.raw.size() <= n) return;
    model::IndividualModel approx_model {genotype_prior_model};
    approx_model.prime(haplotypes);
    const auto approx_posteriors = approx_model.evaluate(genotypes.raw, genotypes.indices, haplotype_likelihoods).posteriors.genotype_log_probabilities;
    reduce(genotypes, approx_posteriors, n);
}

} // namespace

void
PolycloneCaller::fit_sublone_model(const MappableBlock<Haplotype>& haplotypes,
                                   const HaplotypeLikelihoodArray& haplotype_likelihoods,
                                   GenotypePriorModel& genotype_prior_model,
                                   const double haploid_model_evidence,
                                   IndexedGenotypeVectorPair& prev_genotypes,
                                   model::SubcloneModel::InferredLatents& sublonal_inferences) const
{
    model::SubcloneModel::AlgorithmParameters model_params {};
    if (parameters_.max_vb_seeds) model_params.max_seeds = *parameters_.max_vb_seeds;
    model_params.target_max_memory = this->target_max_memory();
    model_params.execution_policy = this->exucution_policy();
    IndexedGenotypeVectorPair curr_genotypes {};
    const auto haploid_log_prior = std::log(parameters_.clonality_prior(1));
    const auto max_clones = std::min(parameters_.max_clones, static_cast<unsigned>(haplotypes.size()));
    for (unsigned clonality {2}; clonality <= max_clones; ++clonality) {
        const auto clonal_model_prior = parameters_.clonality_prior(clonality);
        if (clonal_model_prior == 0.0) break;
        genotype_prior_model.unprime();
        genotype_prior_model.prime(haplotypes);
        const auto max_possible_genotypes = num_max_zygosity_genotypes_noexcept(haplotypes.size(), clonality);
        if (prev_genotypes.raw.empty() || clonality <= 2 || !parameters_.max_genotypes ||
            (max_possible_genotypes && *max_possible_genotypes <= *parameters_.max_genotypes)) {
            curr_genotypes.indices.clear();
            curr_genotypes.raw = generate_all_max_zygosity_genotypes(haplotypes, clonality, curr_genotypes.indices);
        } else {
            const static auto not_included = [] (const auto& genotype, const auto& haplotype) -> bool {
                return !genotype.contains(haplotype); };
            if (prev_genotypes.raw.size() * (haplotypes.size() / 2) > *parameters_.max_genotypes) {
                auto probable_prev_genotypes = prev_genotypes;
                reduce(probable_prev_genotypes, sublonal_inferences.max_evidence_params.genotype_log_probabilities,
                       *parameters_.max_genotypes / (haplotypes.size() / 2));
                std::tie(curr_genotypes.raw, curr_genotypes.indices) = extend_genotypes(probable_prev_genotypes.raw, probable_prev_genotypes.indices, haplotypes, not_included);
            } else {
                std::tie(curr_genotypes.raw, curr_genotypes.indices) = extend_genotypes(prev_genotypes.raw, prev_genotypes.indices, haplotypes, not_included);
            }
        }
        if (parameters_.max_genotypes) reduce(curr_genotypes, haplotypes, genotype_prior_model, haplotype_likelihoods, *parameters_.max_genotypes);
        if (debug_log_) stream(*debug_log_) << "Generated " << curr_genotypes.raw.size() << " genotypes with clonality " << clonality;
        if (curr_genotypes.raw.empty()) break;
        model::SubcloneModel::Priors priors {genotype_prior_model, make_sublone_model_mixture_prior_map(sample(), clonality, parameters_.clone_mixture_prior_concentration)};
        model::SubcloneModel model {{sample()}, priors, model_params};
        model.prime(haplotypes);
        auto inferences = model.evaluate(curr_genotypes.raw, curr_genotypes.indices, haplotype_likelihoods);
        if (debug_log_) stream(*debug_log_) << "Evidence for model with clonality " << clonality << " is " << inferences.approx_log_evidence;
        if (clonality == 2) {
            prev_genotypes = std::move(curr_genotypes);
            sublonal_inferences = std::move(inferences);
            if ((std::log(clonal_model_prior) + sublonal_inferences.approx_log_evidence)
                < (haploid_log_prior + haploid_model_evidence)) break;
        } else {
            if ((std::log(clonal_model_prior) + inferences.approx_log_evidence)
                <= (std::log(parameters_.clonality_prior(clonality - 1)) + sublonal_inferences.approx_log_evidence))  break;
            prev_genotypes = std::move(curr_genotypes);
            sublonal_inferences = std::move(inferences);
        }
    }
}

std::unique_ptr<GenotypePriorModel> PolycloneCaller::make_prior_model(const HaplotypeBlock& haplotypes) const
{
    if (parameters_.prior_model_params) {
        return std::make_unique<CoalescentGenotypePriorModel>(CoalescentModel {
        Haplotype {mapped_region(haplotypes), reference_},
        *parameters_.prior_model_params, haplotypes.size(), CoalescentModel::CachingStrategy::address
        });
    } else {
        return std::make_unique<UniformGenotypePriorModel>();
    }
}

void PolycloneCaller::log(const Latents& latents) const
{
    if (debug_log_) {
        stream(*debug_log_) << "Clonal model posterior is " << latents.model_log_posteriors_.clonal
                            << " and subclonal model posterior is " << latents.model_log_posteriors_.subclonal;
        if (latents.model_log_posteriors_.subclonal > latents.model_log_posteriors_.clonal) {
            stream(*debug_log_) << "Detected subclonality is " << latents.polyploid_genotypes_.front().ploidy();
        }
    }
}

namespace debug { namespace {

template <typename S>
void print_genotype_posteriors(S&& stream,
                               const GenotypeProbabilityMap& genotype_posteriors,
                               const std::size_t n = std::numeric_limits<std::size_t>::max())
{
    const auto m = std::min(n, genotype_posteriors.size());
    if (m == genotype_posteriors.size()) {
        stream << "Printing all genotype posteriors " << '\n';
    } else {
        stream << "Printing top " << m << " genotype posteriors " << '\n';
    }
    using GenotypeReference = std::reference_wrapper<const Genotype<Haplotype>>;
    std::vector<std::pair<GenotypeReference, double>> v {};
    v.reserve(genotype_posteriors.size());
    std::copy(std::cbegin(genotype_posteriors), std::cend(genotype_posteriors), std::back_inserter(v));
    const auto mth = std::next(std::begin(v), m);
    std::partial_sort(std::begin(v), mth, std::end(v),
                      [] (const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
    std::for_each(std::begin(v), mth,
                  [&] (const auto& p) {
                      print_variant_alleles(stream, p.first);
                      stream << " " << p.second << '\n';
                  });
}

void print_genotype_posteriors(const GenotypeProbabilityMap& genotype_posteriors,
                               const std::size_t n = std::numeric_limits<std::size_t>::max())
{
    print_genotype_posteriors(std::cout, genotype_posteriors, n);
}

template <typename S>
void print_candidate_posteriors(S&& stream, const VariantPosteriorVector& candidate_posteriors,
                                const std::size_t n = std::numeric_limits<std::size_t>::max())
{
    const auto m = std::min(n, candidate_posteriors.size());
    if (m == candidate_posteriors.size()) {
        stream << "Printing all candidate variant posteriors " << '\n';
    } else {
        stream << "Printing top " << m << " candidate variant posteriors " << '\n';
    }
    std::vector<std::pair<VariantReference, Phred<double>>> v {};
    v.reserve(candidate_posteriors.size());
    std::copy(std::cbegin(candidate_posteriors), std::cend(candidate_posteriors), std::back_inserter(v));
    const auto mth = std::next(std::begin(v), m);
    std::partial_sort(std::begin(v), mth, std::end(v),
                      [] (const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
    std::for_each(std::begin(v), mth,
                  [&] (const auto& p) {
                      stream << p.first.get() << " " << p.second.probability_true() << '\n';
                  });
}

void print_candidate_posteriors(const VariantPosteriorVector& candidate_posteriors,
                                const std::size_t n = std::numeric_limits<std::size_t>::max())
{
    print_candidate_posteriors(std::cout, candidate_posteriors, n);
}

void log(const GenotypeProbabilityMap& genotype_posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log)
{
    if (trace_log) {
        print_genotype_posteriors(stream(*trace_log), genotype_posteriors);
    }
    if (debug_log) {
        print_genotype_posteriors(stream(*debug_log), genotype_posteriors, 5);
    }
}

void log(const VariantPosteriorVector& candidate_posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log,
         Phred<double> min_posterior)
{
    if (trace_log) {
        print_candidate_posteriors(stream(*trace_log), candidate_posteriors);
    }
    if (debug_log) {
        const auto n = std::count_if(std::cbegin(candidate_posteriors), std::cend(candidate_posteriors),
                                     [=] (const auto& p) { return p.second >= min_posterior; });
        print_candidate_posteriors(stream(*debug_log), candidate_posteriors, std::max(n,  decltype(n) {5}));
    }
}

} // namespace
} // namespace debug

} // namespace octopus
