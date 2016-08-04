//
//  downloader.cpp
//  octopus
//
//  Created by Daniel Cooke on 12/06/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "downloader.hpp"

#include <boost/network/protocol/http.hpp>

#include <basics/aligned_read.hpp>

namespace octopus { namespace coretools {

Downloader::Downloader(const ReferenceGenome& reference, Variant::RegionType::Size max_variant_size)
:
reference_ {reference},
max_variant_size_ {max_variant_size}
{}

std::unique_ptr<VariantGenerator> Downloader::do_clone() const
{
    return std::make_unique<Downloader>(*this);
}

std::vector<Variant> Downloader::do_generate_variants(const GenomicRegion& region)
{
    namespace http = boost::network::http;
    
//    <?xml version="1.0" encoding="UTF-8"?>
//    <!DOCTYPE Query>
//    <Query  virtualSchemaName = "default" formatter = "TSV" header = "0" uniqueRows = "0" count = "" datasetConfigVersion = "0.6" >
//    
//    <Dataset name = "hsapiens_snp" interface = "default" >
//    <Filter name = "chr_name" value = "X"/>
//    <Filter name = "end" value = "10500"/>
//    <Filter name = "start" value = "10000"/>
//    <Attribute name = "refsnp_id" />
//    <Attribute name = "refsnp_source" />
//    <Attribute name = "chr_name" />
//    <Attribute name = "chrom_start" />
//    <Attribute name = "chrom_end" />
//    <Attribute name = "allele" />
//    </Dataset>
//    </Query>
    
    try {
//        http::client::request request("http://www.boost.org/");
//        http::client client;
//        http::client::response response = client.get(request);
//        std::cout << body(response) << std::endl;
        
        std::vector<Variant> result {};
        return result;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        throw;
    }
}
    
std::string Downloader::name() const
{
    return "Download";
}

} // namespace coretools
} // namespace octopus