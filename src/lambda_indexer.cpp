// ==========================================================================
//                                  lambda
// ==========================================================================
// Copyright (c) 2013-2017, Hannes Hauswedell <h2 @ fsfe.org>
// Copyright (c) 2016-2017, Knut Reinert and Freie Universität Berlin
// All rights reserved.
//
// This file is part of Lambda.
//
// Lambda is Free Software: you can redistribute it and/or modify it
// under the terms found in the LICENSE[.md|.rst] file distributed
// together with this file.
//
// Lambda is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
// ==========================================================================
// lambda.cpp: Main File for the main application
// ==========================================================================

#include <initializer_list>

#include <seqan/basic.h>
#include <seqan/arg_parse.h>
#include <seqan/seq_io.h>

#include <iostream>

#define LAMBDA_INDEXER 1 // some things are different for the indexer binary

#include "lambda_indexer.hpp"

using namespace seqan;

// ==========================================================================
// Forwards
// ==========================================================================

inline int
argConv0(LambdaIndexerOptions const & options);

template <BlastProgram p>
inline int
argConv1(LambdaIndexerOptions           const & options,
         BlastProgramSelector<p>        const &);

template <BlastProgram p,
          typename TRedAlph>
inline int
argConv2(LambdaIndexerOptions     const & options,
         BlastProgramSelector<p>  const &,
         TRedAlph                 const &);

template <BlastProgram p,
          typename TRedAlph,
          typename TIndexSpecSpec>
inline int
realMain(LambdaIndexerOptions     const & options,
         BlastProgramSelector<p>  const &,
         TRedAlph                 const &,
         TIndexSpecSpec           const &);

// ==========================================================================
// Functions
// ==========================================================================
// --------------------------------------------------------------------------
// Function main()
// --------------------------------------------------------------------------

// Program entry point.

int main(int argc, char const ** argv)
{
    // Parse the command line.
    seqan::ArgumentParser parser;
    LambdaIndexerOptions options;
    seqan::ArgumentParser::ParseResult res = parseCommandLine(options, argc, argv);

    // If there was an error parsing or built-in argument parser functionality
    // was triggered then we exit the program.  The return code is 1 if there
    // were errors and 0 if there were none.
    if (res != seqan::ArgumentParser::PARSE_OK)
        return res == seqan::ArgumentParser::PARSE_ERROR;

    if (std::string(CMAKE_BUILD_TYPE) != "Release")
        std::cerr << "WARNING: This binary is not built in release mode and will be much slower than it should be!\n";

#ifdef NDEBUG
    int ret = 0;
    try
    {
        ret = argConv0(options);
    } catch (std::bad_alloc const & e)
    {
        std::cerr << "ERROR: Lambda ran out of memory :(\n"
                     "       You need to split your file into smaller segments.\n";
        ret = 1;
    } catch (std::exception const & e)
    {
        std::cerr << e.what() << '\n';
        ret = 1;
    }
    return ret;
#else
    // In debug mode we don't catch the exceptions so that we get a backtrace from SeqAn's handler
    return argConv0(options);
#endif
}

inline int
argConv0(LambdaIndexerOptions const & options)
{
    switch(options.blastProgram)
    {
        case BlastProgram::BLASTN:
            return argConv1(options, BlastProgramSelector<BlastProgram::BLASTN>());
        case BlastProgram::BLASTP:
            return argConv1(options, BlastProgramSelector<BlastProgram::BLASTP>());
        case BlastProgram::BLASTX:
            return argConv1(options, BlastProgramSelector<BlastProgram::BLASTX>());
        case BlastProgram::TBLASTN:
            return argConv1(options, BlastProgramSelector<BlastProgram::TBLASTN>());
        case BlastProgram::TBLASTX:
            return argConv1(options, BlastProgramSelector<BlastProgram::TBLASTX>());
        default:
            break;
    }
    return -1;
}

/// Alphabet reduction
template <BlastProgram p>
inline int
argConv1(LambdaIndexerOptions           const & options,
         BlastProgramSelector<p>        const &)
{
    using TUnred = typename std::conditional<p == BlastProgram::BLASTN, Dna5, AminoAcid>::type;
    using Tp = BlastProgramSelector<p>;
    switch (options.alphReduction)
    {
        case 0:
            return argConv2(options, Tp(), TUnred());
        case 2:
            return argConv2(options, Tp(), ReducedAminoAcid<Murphy10>());
#if 0
        case 10:
            return argConv2(options, ReducedAminoAcid<ClusterReduction<10>>());
        case 1:
            return argConv2(options, AminoAcid10());
        case 8:
            return argConv2(options, ReducedAminoAcid<ClusterReduction<8>>());
        case 12:
            return argConv2(options, ReducedAminoAcid<ClusterReduction<12>>());
#endif
        default:
            return -1;
    }
    return -1;
}

template <BlastProgram p,
          typename TRedAlph>
inline int
argConv2(LambdaIndexerOptions     const & options,
         BlastProgramSelector<p>  const &,
         TRedAlph                 const &)
{
    if (options.algo == "radixsort")
        return realMain(options, BlastProgramSelector<p>(), TRedAlph(), RadixSortSACreateTag());
    else
        return realMain(options, BlastProgramSelector<p>(), TRedAlph(), Nothing());
}

template <BlastProgram p,
          typename TRedAlph,
          typename TIndexSpecSpec>
inline int
realMain(LambdaIndexerOptions     const & options,
         BlastProgramSelector<p>  const &,
         TRedAlph                 const &,
         TIndexSpecSpec           const &)
{
    using TOrigSet  = TCDStringSet<String<OrigSubjAlph<p>>>;
    using TTransSet = TCDStringSet<String<TransAlph<p>>>;

    TTransSet translatedSeqs;
    int ret = 0;

    {
        TOrigSet originalSeqs;
        std::unordered_map<std::string, uint64_t> accToIdRank;

        // ids get saved to disk again immediately and are not kept in memory
        ret = loadSubjSeqsAndIds(originalSeqs, accToIdRank, options);
        if (ret)
            return ret;

        // preserve lengths of untranslated sequences
        if (sIsTranslated(p))
            _saveOriginalSeqLengths(originalSeqs.limits, options);

        if (options.hasSTaxIds)
        {
            std::vector<bool> taxIdIsPresent;
            taxIdIsPresent.reserve(2'000'000);

            // read the mapping file and save relevant mappings to disk
            ret = mapAndDumpTaxIDs(taxIdIsPresent, accToIdRank, length(originalSeqs), options);
            if (ret)
                return ret;

            // read the mapping file and save relevant mappings to disk
            ret = parseAndDumpTaxTree(taxIdIsPresent, options);
            if (ret)
                return ret;
        }

        // translate or swap depending on program
        translateOrSwap(translatedSeqs, originalSeqs, options);
    }

    // dump translated and unreduced sequences (except where they are included in index)
    if ((options.alphReduction != 0) || (options.dbIndexType != DbIndexType::SUFFIX_ARRAY))
        dumpTranslatedSeqs(translatedSeqs, options);

    // see if final sequence set actually fits into index 
    ret = checkIndexSize(translatedSeqs, options, BlastProgramSelector<p>());
    if (ret)
        return ret;

    if (options.dbIndexType == DbIndexType::FM_INDEX)
    {
        using TIndexSpec = TFMIndex<TIndexSpecSpec>;
        generateIndexAndDump<TIndexSpec,TIndexSpecSpec>(translatedSeqs,
                                                        options,
                                                        BlastProgramSelector<p>(),
                                                        TRedAlph(),
                                                        Fwd());
    }
    else if (options.dbIndexType == DbIndexType::BI_FM_INDEX)
    {
//         using TIndexSpec = BidirectionalIndex<TFMIndex<TIndexSpecSpec>>;
        // use regular FM-index tag, because we just create two of them
        using TIndexSpec = TFMIndexInBi<TIndexSpecSpec>;
        // first create the reverse index (which is actually unreversed)
        myPrint(options, 1, "Bi-Directional Index [backward]\n");
        generateIndexAndDump<TIndexSpec,TIndexSpecSpec>(translatedSeqs,
                                                        options,
                                                        BlastProgramSelector<p>(),
                                                        TRedAlph(),
                                                        Rev());
        // then create the regular/forward fm-index (which is actually reversed)
        myPrint(options, 1, "Bi-Directional Index [forward]\n");
        generateIndexAndDump<TIndexSpec,TIndexSpecSpec>(translatedSeqs,
                                                        options,
                                                        BlastProgramSelector<p>(),
                                                        TRedAlph(),
                                                        Fwd());
    }
    else
    {
        using TIndexSpec = IndexSa<TIndexSpecSpec>;
        generateIndexAndDump<TIndexSpec,TIndexSpecSpec>(translatedSeqs,
                                                        options,
                                                        BlastProgramSelector<p>(),
                                                        TRedAlph(),
                                                        Fwd());
    }

    // dump options
    for (auto && s : std::initializer_list<std::pair<std::string, std::string>>
         {
             { options.indexDir + "/option:db_index_type",   std::to_string(static_cast<uint32_t>(options.dbIndexType))},
             { options.indexDir + "/option:alph_original",   std::string(_alphName(OrigSubjAlph<p>())) },
             { options.indexDir + "/option:alph_translated", std::string(_alphName(TransAlph<p>())) },
             { options.indexDir + "/option:alph_reduced",    std::string(_alphName(TRedAlph())) },
             { options.indexDir + "/option:genetic_code",    std::to_string(options.geneticCode) },
             { options.indexDir + "/option:subj_seq_len_bits", std::to_string(sizeof(SizeTypePos_<TRedAlph>) * 8)},
             { options.indexDir + "/option:generation",      std::to_string(indexGeneration) },
         })
    {
        std::ofstream f{std::get<0>(s).c_str(),  std::ios_base::out | std::ios_base::binary};
        f << std::get<1>(s);
        f.close();
    }

    return 0;
}

