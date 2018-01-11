// Copyright (c) 2017 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef samples_hpp
#define samples_hpp

#include <string>
#include <vector>

#include "facet.hpp"

namespace octopus { namespace csr {

class Samples : public Facet
{
public:
    using ResultType = std::vector<std::string>;
    
    Samples(std::vector<std::string> samples);

private:
    static const std::string name_;
    
    std::vector<std::string> samples_;
    
    const std::string& do_name() const noexcept override { return name_; }
    Facet::ResultType do_get() const override;
};

} // namespace csr
} // namespace octopus

#endif