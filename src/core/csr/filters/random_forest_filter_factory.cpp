// Copyright (c) 2015-2019 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "random_forest_filter_factory.hpp"

#include "utils/string_utils.hpp"
#include "exceptions/missing_file_error.hpp"
#include "../measures/measure_factory.hpp"
#include "somatic_random_forest_filter.hpp"
#include "denovo_random_forest_filter.hpp"

namespace octopus { namespace csr {

namespace {

std::vector<MeasureWrapper> parse_measures(const std::vector<std::string>& measure_names)
{
    std::vector<MeasureWrapper> result{};
    result.reserve(measure_names.size());
    std::transform(std::cbegin(measure_names), std::cend(measure_names), std::back_inserter(result), make_measure);
    return result;
}

} // namespace

static const auto default_measure_names = utils::split("AC AD ADP AF ARF BQ CC CRF DAD DAF DP DPC ER ERS FRF GC GQ GQD NC MC MF MP MRC MQ MQ0 MQD PP PPD QD QUAL REFCALL REB RSB RTB SB SD SF SHC SMQ SOMATIC STRL STRP VL", ' ');

RandomForestFilterFactory::RandomForestFilterFactory()
: ranger_forests_ {}
, forest_types_ {}
, temp_directory_ {}
{
    measures_ = parse_measures(default_measure_names);
}

class MissingRangerForest : public MissingFileError
{
    std::string do_where() const override { return "RandomForestFilterFactory::check_forests"; }
public:
    MissingRangerForest(boost::filesystem::path file) : MissingFileError {std::move(file), "forest"} {}
};

void check_forests(const std::vector<RandomForestFilterFactory::Path>& ranger_forests,
                   const std::vector<RandomForestFilterFactory::ForestType>& forest_types)
{
    if (ranger_forests.size() != forest_types.size()) {
        throw std::runtime_error {"Bad specification of forests"};
    }
    for (const auto& forest : ranger_forests) {
        if (!boost::filesystem::exists(forest)) {
            throw MissingRangerForest {forest};
        }
    }
}

RandomForestFilterFactory::RandomForestFilterFactory(std::vector<Path> ranger_forests,
                                                     std::vector<ForestType> forest_types,
                                                     Path temp_directory,
                                                     Options options)
: ranger_forests_ {std::move(ranger_forests)}
, forest_types_ {std::move(forest_types)}
, temp_directory_ {std::move(temp_directory)}
, options_ {std::move(options)}
{
    check_forests(ranger_forests_, forest_types_);
    measures_ = parse_measures(default_measure_names);
}

std::unique_ptr<VariantCallFilterFactory> RandomForestFilterFactory::do_clone() const
{
    return std::make_unique<RandomForestFilterFactory>(*this);
}

std::vector<MeasureWrapper> RandomForestFilterFactory::measures() const
{
    return measures_;
}

std::unique_ptr<VariantCallFilter>
RandomForestFilterFactory::do_make(FacetFactory facet_factory,
                                   VariantCallFilter::OutputOptions output_config,
                                   boost::optional<ProgressMeter&> progress,
                                   VariantCallFilter::ConcurrencyPolicy threading) const
{
    if (ranger_forests_.size() == 1) {
        assert(forest_types_.size() == 1);
        switch (forest_types_.front()) {
            case ForestType::somatic:
                return std::make_unique<SomaticRandomForestVariantCallFilter>(std::move(facet_factory), measures_, ranger_forests_[0],
                                                                              output_config, threading, temp_directory_, options_, progress);
            case ForestType::denovo:
                return std::make_unique<DeNovoRandomForestVariantCallFilter>(std::move(facet_factory), measures_, ranger_forests_[0],
                                                                             output_config, threading, temp_directory_, options_, progress);
            case ForestType::germline:
            default:
                return std::make_unique<RandomForestFilter>(std::move(facet_factory), measures_, ranger_forests_[0],
                                                            output_config, threading, temp_directory_, options_, progress);
        }
    } else {
        assert(ranger_forests_.size() == 2);
        assert(forest_types_.front() == ForestType::germline);
        if (forest_types_.back() == ForestType::somatic) {
            return std::make_unique<SomaticRandomForestVariantCallFilter>(std::move(facet_factory), measures_,
                                                                          ranger_forests_[0], ranger_forests_[1],
                                                                          output_config, threading, temp_directory_, options_, progress);
        } else {
            return std::make_unique<DeNovoRandomForestVariantCallFilter>(std::move(facet_factory), measures_,
                                                                         ranger_forests_[0], ranger_forests_[1],
                                                                         output_config, threading, temp_directory_, options_, progress);
        }
    }
}

} // namespace csr
} // namespace octopus
