// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dnatests/Defs.h"
#include "dnatests/FakeStream.h"

#include "dna/StreamReader.h"
#include "dna/StreamWriter.h"

#include <pma/resources/DefaultMemoryResource.h>

class StreamWriterTest : public ::testing::Test {
    public:
        ~StreamWriterTest();

    protected:
        void SetUp() override {
            writer = dna::StreamWriter::create(&stream, &memRes);
            reader = dna::StreamReader::create(&stream, dna::DataLayer::All, 0u, &memRes);
        }

        void TearDown() override {
            dna::StreamReader::destroy(reader);
            dna::StreamWriter::destroy(writer);
        }

    protected:
        dnatests::FakeStream stream;
        pma::DefaultMemoryResource memRes;
        dna::StreamWriter* writer;
        dna::StreamReader* reader;
};
