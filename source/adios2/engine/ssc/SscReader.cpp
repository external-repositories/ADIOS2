/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * SscReader.cpp
 *
 *  Created on: Nov 1, 2018
 *      Author: Jason Wang
 */

#include "SscReader.tcc"
#include "adios2/helper/adiosComm.h"
#include "adios2/helper/adiosCommMPI.h"
#include "adios2/helper/adiosFunctions.h"
#include "adios2/helper/adiosJSONcomplex.h"
#include "nlohmann/json.hpp"

namespace adios2
{
namespace core
{
namespace engine
{

SscReader::SscReader(IO &io, const std::string &name, const Mode mode,
                     helper::Comm comm)
: Engine("SscReader", io, name, mode, std::move(comm))
{
    TAU_SCOPED_TIMER_FUNC();

    helper::GetParameter(m_IO.m_Parameters, "MpiMode", m_MpiMode);
    helper::GetParameter(m_IO.m_Parameters, "Verbose", m_Verbosity);
    helper::GetParameter(m_IO.m_Parameters, "OpenTimeoutSecs",
                         m_OpenTimeoutSecs);

    SyncMpiPattern();
    m_ReaderRank = m_Comm.Rank();
    m_ReaderSize = m_Comm.Size();
    MPI_Comm_rank(m_StreamComm, &m_StreamRank);
    MPI_Comm_size(m_StreamComm, &m_StreamSize);
}

SscReader::~SscReader() { TAU_SCOPED_TIMER_FUNC(); }

StepStatus SscReader::BeginStep(const StepMode stepMode,
                                const float timeoutSeconds)
{
    TAU_SCOPED_TIMER_FUNC();

    ++m_CurrentStep;

    if (m_Verbosity >= 5)
    {
        std::cout << "SscReader::BeginStep, World Rank " << m_StreamRank
                  << ", Reader Rank " << m_ReaderRank << ", Step "
                  << m_CurrentStep << std::endl;
    }

    if (m_CurrentStep == 0 || m_WriterDefinitionsLocked == false ||
        m_ReaderSelectionsLocked == false)
    {
        m_ReceivedRanks.clear();
        m_Buffer.resize(1, 0);
        m_GlobalWritePattern.clear();
        m_GlobalWritePattern.resize(m_StreamSize);
        m_LocalReadPattern.clear();
        m_GlobalWritePatternJson.clear();
        bool finalStep = SyncWritePattern();
        if (finalStep)
        {
            return StepStatus::EndOfStream;
        }

        MPI_Win_create(NULL, 0, 1, MPI_INFO_NULL, m_StreamComm, &m_MpiWin);
    }
    else
    {
        if (m_MpiMode == "twosided")
        {
            MPI_Status statuses[m_MpiRequests.size()];
            MPI_Waitall(m_MpiRequests.size(), m_MpiRequests.data(), statuses);
            m_MpiRequests.clear();
        }
        else if (m_MpiMode == "onesidedfencepush")
        {
            MPI_Win_fence(0, m_MpiWin);
        }
        else if (m_MpiMode == "onesidedpostpush")
        {
            MPI_Win_wait(m_MpiWin);
        }
        else if (m_MpiMode == "onesidedfencepull")
        {
            MPI_Win_fence(0, m_MpiWin);
        }
        else if (m_MpiMode == "onesidedpostpull")
        {
            MPI_Win_complete(m_MpiWin);
        }
    }

    for (const auto &r : m_GlobalWritePattern)
    {
        for (auto &v : r)
        {
            if (v.shapeId == ShapeID::GlobalValue ||
                v.shapeId == ShapeID::LocalValue)
            {
                std::vector<char> value(v.bufferCount);
                if (m_CurrentStep == 0 || m_WriterDefinitionsLocked == false ||
                    m_ReaderSelectionsLocked == false)
                {
                    std::memcpy(value.data(), v.value.data(), v.value.size());
                }
                else
                {
                    std::memcpy(value.data(), m_Buffer.data() + v.bufferStart,
                                v.bufferCount);
                }
                if (v.type == DataType::None)
                {
                    throw(std::runtime_error("unknown data type"));
                }
                else if (v.type == DataType::String)
                {
                    auto variable = m_IO.InquireVariable<std::string>(v.name);
                    if (variable)
                    {
                        variable->m_Value =
                            std::string(value.begin(), value.end());
                        variable->m_Min =
                            std::string(value.begin(), value.end());
                        variable->m_Max =
                            std::string(value.begin(), value.end());
                    }
                }
#define declare_type(T)                                                        \
    else if (v.type == helper::GetDataType<T>())                               \
    {                                                                          \
        auto variable = m_IO.InquireVariable<T>(v.name);                       \
        if (variable)                                                          \
        {                                                                      \
            std::memcpy(&variable->m_Min, value.data(), value.size());         \
            std::memcpy(&variable->m_Max, value.data(), value.size());         \
            std::memcpy(&variable->m_Value, value.data(), value.size());       \
        }                                                                      \
    }
                ADIOS2_FOREACH_STDTYPE_1ARG(declare_type)
#undef declare_type
                else { throw(std::runtime_error("unknown data type")); }
            }
        }
    }

    if (m_Buffer[0] == 1)
    {
        return StepStatus::EndOfStream;
    }

    return StepStatus::OK;
}

void SscReader::PerformGets() {}

size_t SscReader::CurrentStep() const { return m_CurrentStep; }

void SscReader::EndStep()
{
    TAU_SCOPED_TIMER_FUNC();

    if (m_Verbosity >= 5)
    {
        std::cout << "SscReader::EndStep, World Rank " << m_StreamRank
                  << ", Reader Rank " << m_ReaderRank << ", Step "
                  << m_CurrentStep << std::endl;
    }

    if (m_WriterDefinitionsLocked &&
        m_ReaderSelectionsLocked) // fixed IO pattern
    {
        if (m_CurrentStep == 0)
        {
            MPI_Win_free(&m_MpiWin);
            SyncReadPattern();
            MPI_Win_create(m_Buffer.data(), m_Buffer.size(), 1, MPI_INFO_NULL,
                           m_StreamComm, &m_MpiWin);
        }
        if (m_MpiMode == "twosided")
        {
            for (const auto &i : m_AllReceivingWriterRanks)
            {
                m_MpiRequests.emplace_back();
                MPI_Irecv(m_Buffer.data() + i.second.first, i.second.second,
                          MPI_CHAR, i.first, 0, m_StreamComm,
                          &m_MpiRequests.back());
            }
        }
        else if (m_MpiMode == "onesidedfencepush")
        {
            MPI_Win_fence(0, m_MpiWin);
        }
        else if (m_MpiMode == "onesidedpostpush")
        {
            MPI_Win_post(m_MpiAllWritersGroup, 0, m_MpiWin);
        }
        else if (m_MpiMode == "onesidedfencepull")
        {
            MPI_Win_fence(0, m_MpiWin);
            for (const auto &i : m_AllReceivingWriterRanks)
            {
                MPI_Get(m_Buffer.data() + i.second.first, i.second.second,
                        MPI_CHAR, i.first, 0, i.second.second, MPI_CHAR,
                        m_MpiWin);
            }
        }
        else if (m_MpiMode == "onesidedpostpull")
        {
            MPI_Win_start(m_MpiAllWritersGroup, 0, m_MpiWin);
            for (const auto &i : m_AllReceivingWriterRanks)
            {
                MPI_Get(m_Buffer.data() + i.second.first, i.second.second,
                        MPI_CHAR, i.first, 0, i.second.second, MPI_CHAR,
                        m_MpiWin);
            }
        }
    }
    else // flexible IO pattern
    {
        MPI_Win_free(&m_MpiWin);
        if (m_CurrentStep == 0)
        {
            SyncReadPattern();
        }
    }
}

// PRIVATE

void SscReader::SyncMpiPattern()
{
    TAU_SCOPED_TIMER_FUNC();

    auto appRankMaps =
        helper::Handshake(m_Name, 'r', m_OpenTimeoutSecs, CommAsMPI(m_Comm));

    MPI_Group worldGroup;
    MPI_Group streamGroup;

    MPI_Comm_group(MPI_COMM_WORLD, &worldGroup);
    MPI_Group_incl(worldGroup, appRankMaps[1].size(), appRankMaps[1].data(),
                   &m_MpiAllWritersGroup);
    MPI_Group_incl(worldGroup, appRankMaps[0].size(), appRankMaps[0].data(),
                   &streamGroup);

    MPI_Comm_create_group(MPI_COMM_WORLD, streamGroup, 0, &m_StreamComm);
}

bool SscReader::SyncWritePattern()
{
    TAU_SCOPED_TIMER_FUNC();
    if (m_Verbosity >= 5)
    {
        std::cout << "SscReader::SyncWritePattern, World Rank " << m_StreamRank
                  << ", Reader Rank " << m_ReaderRank << ", Step "
                  << m_CurrentStep << std::endl;
    }

    // aggregate global write pattern
    size_t localSize = 0;
    size_t maxLocalSize;
    MPI_Allreduce(&localSize, &maxLocalSize, 1, MPI_UNSIGNED_LONG_LONG, MPI_MAX,
                  m_StreamComm);
    std::vector<char> localVec(maxLocalSize, '\0');
    std::vector<char> globalVec(maxLocalSize * m_StreamSize);
    MPI_Allgather(localVec.data(), maxLocalSize, MPI_CHAR, globalVec.data(),
                  maxLocalSize, MPI_CHAR, m_StreamComm);

    ssc::LocalJsonToGlobalJson(globalVec, maxLocalSize, m_StreamSize,
                               m_GlobalWritePatternJson);

    for (int i = 0; i < m_StreamSize; ++i)
    {
        if (m_GlobalWritePatternJson[i] == nullptr)
        {
            continue;
        }
        auto &patternJson = m_GlobalWritePatternJson[i]["FinalStep"];
        if (patternJson != nullptr)
        {
            if (patternJson.get<bool>())
            {
                return true;
            }
        }
    }

    ssc::JsonToBlockVecVec(m_GlobalWritePatternJson, m_GlobalWritePattern);

    for (const auto &blockVec : m_GlobalWritePattern)
    {
        for (const auto &b : blockVec)
        {
            if (b.type == DataType::None)
            {
                throw(std::runtime_error("unknown data type"));
            }
#define declare_type(T)                                                        \
    else if (b.type == helper::GetDataType<T>())                               \
    {                                                                          \
        auto v = m_IO.InquireVariable<T>(b.name);                              \
        if (not v)                                                             \
        {                                                                      \
            Dims vStart = b.start;                                             \
            Dims vShape = b.shape;                                             \
            if (!helper::IsRowMajor(m_IO.m_HostLanguage))                      \
            {                                                                  \
                std::reverse(vStart.begin(), vStart.end());                    \
                std::reverse(vShape.begin(), vShape.end());                    \
            }                                                                  \
            if (b.shapeId == ShapeID::GlobalValue)                             \
            {                                                                  \
                m_IO.DefineVariable<T>(b.name);                                \
            }                                                                  \
            else if (b.shapeId == ShapeID::GlobalArray)                        \
            {                                                                  \
                m_IO.DefineVariable<T>(b.name, vShape, vStart, vShape);        \
            }                                                                  \
            else if (b.shapeId == ShapeID::LocalValue)                         \
            {                                                                  \
                m_IO.DefineVariable<T>(b.name, {adios2::LocalValueDim});       \
            }                                                                  \
            else if (b.shapeId == ShapeID::LocalArray)                         \
            {                                                                  \
                m_IO.DefineVariable<T>(b.name, {}, {}, vShape);                \
            }                                                                  \
        }                                                                      \
    }
            ADIOS2_FOREACH_STDTYPE_1ARG(declare_type)
#undef declare_type
            else { throw(std::runtime_error("unknown data type")); }
        }
    }

    for (int i = 0; i < m_StreamSize; ++i)
    {
        if (m_GlobalWritePatternJson[i] == nullptr)
        {
            continue;
        }

        auto &patternJson = m_GlobalWritePatternJson[i]["Pattern"];
        if (patternJson != nullptr)
        {
            m_WriterDefinitionsLocked = patternJson.get<bool>();
        }

        auto &attributesJson = m_GlobalWritePatternJson[i]["Attributes"];
        if (attributesJson == nullptr)
        {
            continue;
        }
        for (const auto &attributeJson : attributesJson)
        {
            const DataType type(helper::GetDataTypeFromString(
                attributeJson["Type"].get<std::string>()));
            if (type == DataType::None)
            {
            }
#define declare_type(T)                                                        \
    else if (type == helper::GetDataType<T>())                                 \
    {                                                                          \
        const auto &attributes = m_IO.GetAttributes();                         \
        auto it = attributes.find(attributeJson["Name"].get<std::string>());   \
        if (it == attributes.end())                                            \
        {                                                                      \
            if (attributeJson["IsSingleValue"].get<bool>())                    \
            {                                                                  \
                m_IO.DefineAttribute<T>(                                       \
                    attributeJson["Name"].get<std::string>(),                  \
                    attributeJson["Value"].get<T>());                          \
            }                                                                  \
            else                                                               \
            {                                                                  \
                m_IO.DefineAttribute<T>(                                       \
                    attributeJson["Name"].get<std::string>(),                  \
                    attributeJson["Array"].get<std::vector<T>>().data(),       \
                    attributeJson["Array"].get<std::vector<T>>().size());      \
            }                                                                  \
        }                                                                      \
    }
            ADIOS2_FOREACH_ATTRIBUTE_STDTYPE_1ARG(declare_type)
#undef declare_type
        }
    }
    return false;
}

void SscReader::SyncReadPattern()
{
    TAU_SCOPED_TIMER_FUNC();

    if (m_Verbosity >= 5)
    {
        std::cout << "SscReader::SyncReadPattern, World Rank " << m_StreamRank
                  << ", Reader Rank " << m_ReaderRank << ", Step "
                  << m_CurrentStep << std::endl;
    }

    if (m_ReaderRank == 0)
    {
        m_LocalReadPatternJson["Pattern"] = m_ReaderSelectionsLocked;
    }

    std::string localStr = m_LocalReadPatternJson.dump();

    // aggregate global read pattern
    size_t localSize = localStr.size();
    size_t maxLocalSize;
    MPI_Allreduce(&localSize, &maxLocalSize, 1, MPI_UNSIGNED_LONG_LONG, MPI_MAX,
                  m_StreamComm);
    std::vector<char> localVec(maxLocalSize, '\0');
    std::memcpy(localVec.data(), localStr.c_str(), localStr.size());
    std::vector<char> globalVec(maxLocalSize * m_StreamSize);
    MPI_Allgather(localVec.data(), maxLocalSize, MPI_CHAR, globalVec.data(),
                  maxLocalSize, MPI_CHAR, m_StreamComm);

    // deserialize global metadata Json
    nlohmann::json globalJson;
    try
    {
        for (size_t i = 0; i < m_StreamSize; ++i)
        {
            if (globalVec[i * maxLocalSize] == '\0')
            {
                globalJson[i] = nullptr;
            }
            else
            {
                globalJson[i] = nlohmann::json::parse(
                    globalVec.begin() + i * maxLocalSize,
                    globalVec.begin() + (i + 1) * maxLocalSize);
            }
        }
    }
    catch (std::exception &e)
    {
        throw(std::runtime_error(
            std::string("corrupted global read pattern metadata, ") +
            std::string(e.what())));
    }

    ssc::JsonToBlockVecVec(m_GlobalWritePatternJson, m_GlobalWritePattern);
    m_AllReceivingWriterRanks =
        ssc::CalculateOverlap(m_GlobalWritePattern, m_LocalReadPattern);
    CalculatePosition(m_GlobalWritePattern, m_AllReceivingWriterRanks);
    size_t totalDataSize = 0;
    for (auto i : m_AllReceivingWriterRanks)
    {
        totalDataSize += i.second.second;
    }
    m_Buffer.resize(totalDataSize);

    if (m_Verbosity >= 10)
    {
        ssc::PrintBlockVec(m_LocalReadPattern, "Local Read Pattern");
    }
}

void SscReader::CalculatePosition(ssc::BlockVecVec &bvv,
                                  ssc::RankPosMap &allRanks)
{
    TAU_SCOPED_TIMER_FUNC();

    size_t bufferPosition = 0;

    for (size_t rank = 0; rank < bvv.size(); ++rank)
    {
        bool hasOverlap = false;
        for (const auto r : allRanks)
        {
            if (r.first == rank)
            {
                hasOverlap = true;
                break;
            }
        }
        if (hasOverlap)
        {
            allRanks[rank].first = bufferPosition;
            auto &bv = bvv[rank];
            for (auto &b : bv)
            {
                b.bufferStart += bufferPosition;
            }
            size_t currentRankTotalSize = ssc::TotalDataSize(bv);
            allRanks[rank].second = currentRankTotalSize + 1;
            bufferPosition += currentRankTotalSize + 1;
        }
    }
}

#define declare_type(T)                                                        \
    void SscReader::DoGetSync(Variable<T> &variable, T *data)                  \
    {                                                                          \
        GetDeferredCommon(variable, data);                                     \
        PerformGets();                                                         \
    }                                                                          \
    void SscReader::DoGetDeferred(Variable<T> &variable, T *data)              \
    {                                                                          \
        GetDeferredCommon(variable, data);                                     \
    }                                                                          \
    std::vector<typename Variable<T>::Info> SscReader::DoBlocksInfo(           \
        const Variable<T> &variable, const size_t step) const                  \
    {                                                                          \
        return BlocksInfoCommon(variable, step);                               \
    }
ADIOS2_FOREACH_STDTYPE_1ARG(declare_type)
#undef declare_type

void SscReader::DoClose(const int transportIndex)
{
    TAU_SCOPED_TIMER_FUNC();
    if (m_Verbosity >= 5)
    {
        std::cout << "SscReader::DoClose, World Rank " << m_StreamRank
                  << ", Reader Rank " << m_ReaderRank << std::endl;
    }
    if (m_WriterDefinitionsLocked && m_ReaderSelectionsLocked)
    {
        MPI_Win_free(&m_MpiWin);
    }
}

} // end namespace engine
} // end namespace core
} // end namespace adios2
