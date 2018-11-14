//
// Copyright 2018 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#ifndef TRACE_SERIALIZATION_H
#define TRACE_SERIALIZATION_H

#include "pxr/pxr.h"
#include "pxr/base/trace/api.h"
#include "pxr/base/trace/collection.h"

#include <istream>
#include <ostream>
#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

////////////////////////////////////////////////////////////////////////////////
/// \class TraceSerialization
///
/// This class contains methods to read and write TraceCollection.
///
class TraceSerialization {
public:
    /// Writes \p col to \p ostr.
    /// Returns true if the write was successful, false otherwise.
    TRACE_API static bool Write(std::ostream& ostr,
        const std::shared_ptr<TraceCollection>& col);

    /// Writes \p collections to \p ostr.
    /// Returns true if the write was successful, false otherwise.
    TRACE_API static bool Write(
        std::ostream& ostr,
        const std::vector<std::shared_ptr<TraceCollection>>& collections);

    /// Tries to create a TraceCollection from the contexts of \p istr.
    /// Returns a pointer to the created collection if it was successful.
    /// If there is an error reading \p istr, \p error will be populated with a
    /// description.
    TRACE_API static std::unique_ptr<TraceCollection> Read(std::istream& istr,
        std::string* error = nullptr);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // TRACE_SERIALIZATION_H