/*
 * Copyright (c) 2011, Intel Corporation.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "dumpGetFeatures_r10b.h"
#include "globals.h"
#include "grpDefs.h"
#include "createACQASQ_r10b.h"
#include "../Cmds/getFeatures.h"
#include "../Utils/kernelAPI.h"


DumpGetFeatures_r10b::DumpGetFeatures_r10b(int fd, string grpName,
    string testName, ErrorRegs errRegs) :
    Test(fd, grpName, testName, SPECREV_10b, errRegs)
{
    // 66 chars allowed:     xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    mTestDesc.SetCompliance("revision 1.0b, section 7");
    mTestDesc.SetShort(     "Issue the get features cmd");
    // No string size limit for the long description
    mTestDesc.SetLong(
        "Issue the get features cmd to the ASQ. Request various feature "
        "identifiers which are deemed important enough to retrieve for all "
        "tests to view easily");
}


DumpGetFeatures_r10b::~DumpGetFeatures_r10b()
{
    ///////////////////////////////////////////////////////////////////////////
    // Allocations taken from the heap and not under the control of the
    // RsrcMngr need to be freed/deleted here.
    ///////////////////////////////////////////////////////////////////////////
}


DumpGetFeatures_r10b::
DumpGetFeatures_r10b(const DumpGetFeatures_r10b &other) : Test(other)
{
    ///////////////////////////////////////////////////////////////////////////
    // All pointers in this object must be NULL, never allow shallow or deep
    // copies, see Test::Clone() header comment.
    ///////////////////////////////////////////////////////////////////////////
}


DumpGetFeatures_r10b &
DumpGetFeatures_r10b::operator=(const DumpGetFeatures_r10b &other)
{
    ///////////////////////////////////////////////////////////////////////////
    // All pointers in this object must be NULL, never allow shallow or deep
    // copies, see Test::Clone() header comment.
    ///////////////////////////////////////////////////////////////////////////
    Test::operator=(other);
    return *this;
}


bool
DumpGetFeatures_r10b::RunCoreTest()
{
    /** \verbatim
     * Assumptions:
     * 1) The ASQ & ACQ's have been created by the RsrcMngr for group lifetime
     * 2) All interrupts are disabled.
     *  \endverbatim
     */
    uint32_t isrCount;

    KernelAPI::DumpKernelMetrics(mFd,
        FileSystem::PrepLogFile(mGrpName, mTestName, "kmetrics", "before"));

    // Lookup objs which were created in a prior test within group
    SharedASQPtr asq = CAST_TO_ASQ(gRsrcMngr->GetObj(ASQ_GROUP_ID))
    SharedACQPtr acq = CAST_TO_ACQ(gRsrcMngr->GetObj(ACQ_GROUP_ID))

    // Assuming the cmd we issue will result in only a single CE
    if (acq->ReapInquiry(isrCount) != 0) {
        LOG_ERR("The ACQ should not have any CE's waiting before testing");
        throw exception();
    }

    SendGetFeaturesNumOfQueues(asq, acq);

    KernelAPI::DumpKernelMetrics(mFd,
        FileSystem::PrepLogFile(mGrpName, mTestName, "kmetrics", "after"));
    return true;
}


void
DumpGetFeatures_r10b::SendGetFeaturesNumOfQueues(SharedASQPtr asq,
    SharedACQPtr acq)
{
    uint16_t numCE;
    uint32_t isrCount;


    LOG_NRM("Create get features");
    SharedGetFeaturesPtr gfNumQ = SharedGetFeaturesPtr(new GetFeatures(mFd));
    LOG_NRM("Force get features to request number of queues");
    gfNumQ->SetFID(GetFeatures::FID_NUM_QUEUES);
    gfNumQ->Dump(
        FileSystem::PrepLogFile(mGrpName, mTestName, "GetFeat", "NumOfQueue"),
        "The get features number of queues cmd");


    LOG_NRM("Send the get features cmd to hdw");
    asq->Send(gfNumQ);
    asq->Dump(FileSystem::PrepLogFile(mGrpName, mTestName, "asq",
        "GetFeat.NumOfQueue"),
        "Just B4 ringing SQ0 doorbell, dump entire SQ contents");
    asq->Ring();


    LOG_NRM("Wait for the CE to arrive in ACQ");
    if (acq->ReapInquiryWaitSpecify(DEFAULT_CMD_WAIT_ms, 1, numCE, isrCount)
        == false) {

        LOG_ERR("Unable to see completion of get features cmd");
        acq->Dump(
            FileSystem::PrepLogFile(mGrpName, mTestName, "acq",
            "GetFeat.NumOfQueue"),
            "Unable to see any CE's in CQ0, dump entire CQ contents");
        throw exception();
    } else if (numCE != 1) {
        LOG_ERR("The ACQ should only have 1 CE as a result of a cmd");
        throw exception();
    }
    acq->Dump(FileSystem::PrepLogFile(mGrpName, mTestName, "acq",
        "GetFeat.NumOfQueue"),
        "Just B4 reaping CQ0, dump entire CQ contents");

    {
        uint16_t ceRemain;
        uint16_t numReaped;


        LOG_NRM("The CQ's metrics before reaping holds head_ptr needed");
        struct nvme_gen_cq acqMetrics = acq->GetQMetrics();
        KernelAPI::LogCQMetrics(acqMetrics);

        LOG_NRM("Reaping CE from ACQ, requires memory to hold reaped CE");
        SharedMemBufferPtr ceMemCap = SharedMemBufferPtr(new MemBuffer());
        if ((numReaped = acq->Reap(ceRemain, ceMemCap, isrCount, numCE, true))
            != 1) {

            LOG_ERR("Verified there was 1 CE, but reaping produced %d",
                numReaped);
            throw exception();
        }
        LOG_NRM("The reaped CE is...");
        acq->LogCE(acqMetrics.head_ptr);
        acq->DumpCE(acqMetrics.head_ptr, FileSystem::PrepLogFile
            (mGrpName, mTestName, "CE", "GetFeat.NumOfQueue"),
            "The CE of the Get Features cmd; Number of Q's feature ID:");

        union CE ce = acq->PeekCE(acqMetrics.head_ptr);
        ProcessCE::ValidateStatus(ce);  // throws upon error

        // Update the Informative singleton for all tests to see and use
        gInformative->SetGetFeaturesNumberOfQueues(ce.t.dw0);
    }
}
