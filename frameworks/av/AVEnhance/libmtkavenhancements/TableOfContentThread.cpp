/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "TableOfContentThread"
#define MTK_LOG_ENABLE 1
#include <utils/Log.h>

#include "TableOfContentThread.h"
#include <cutils/sched_policy.h>
#include <cutils/iosched_policy.h>

#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <pthread.h>
#include <sched.h>
#include <log/log.h>

#define ENABLE_TOC_DBUGLOG

#ifdef ENABLE_TOC_DBUGLOG

#define TOC_PRINT_DBG(fmt, arg...) SLOGD(fmt, ##arg)
#define TOC_PRINT_WARN(fmt, arg...) SLOGW(fmt, ##arg)
#define TOC_PRINT_ERR(fmt, arg...)  SLOGE("Err: %5d:, " fmt, __LINE__, ##arg)

#else
#define TOC_PRINT_DBG(a,...)
#define TOC_PRINT_WARN(a,...)
#define TOC_PRINT_ERR(a,...)
#endif



namespace android
{

TableOfContentThread::TableOfContentThread()
{
    TOC_PRINT_DBG("TableOfContentThread Construct !%p", this);
    mRunning = false;
    mStopped = true;
    mEntryCount = 0;
    mTocInterval = 0;
    mThread = -1;
    mTocComplete = false;
    mDuration = 0;
    mFirstFramePos = 0;
    mCurFilePos = mFirstFramePos;
    mNextFilePos = 0;
    mTocIntervalLeft = 1;
    mFrameNum = 0;
    mTocTimeUs = 0;
    mTocSize = 0;
    m_Toc.size = 0;
    mCurPaktPos = 0;
    mNextPaktPos = 0;
    mSeekPaktPos = 0;


}

TableOfContentThread::~TableOfContentThread()
{
    TOC_PRINT_DBG("~TableOfContentThread %p!", this);
    stopTOCThread();
}

void TableOfContentThread::startTOCThread(off64_t firstFramePos, uint32_t TocSize, uint32_t TocInterval)
{
    TOC_PRINT_DBG("TableOfContentThread::startTOCThread %p", this);

    if (mRunning)
    {
        return;
    }

    mFirstFramePos = firstFramePos;
    mCurFilePos = mFirstFramePos;
    mCurPaktPos = mFirstPaktPos;
    mStopped = false;
    mTocSize = TocSize;
    mTocInterval = TocInterval;
    m_Toc.TocPos.setCapacity(mTocSize);
    m_Toc.TocTS.setCapacity(mTocSize);
    m_Toc.PaktPos.setCapacity(mTocSize);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&mThread, &attr, threadWrapper, this);
    pthread_attr_destroy(&attr);
    mRunning = true;
}
void TableOfContentThread::stopTOCThread()
{
    TOC_PRINT_DBG("stopTOCThread %p", this);

    if (mRunning)
    {
        void *dummy = 0;
        mStopped = true;
        pthread_join(mThread, &dummy);
        TOC_PRINT_DBG("stopTOCThread pthread_join");
        mRunning = false;
    }
}

//static
void *TableOfContentThread::threadWrapper(void *me)
{
    TOC_PRINT_DBG("TableOfContentThread::threadWrapper");
    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_BACKGROUND);// Normal 10

    if (android_set_ioprio(PRIO_PROCESS, IoSchedClass_NONE, 0))
    {
        TOC_PRINT_ERR("set io prio fail:: tid= %d !!!", gettid());
    }

    static_cast<TableOfContentThread *>(me)->threadEntry();

    return NULL;
}

void TableOfContentThread::threadEntry()
{
    prctl(PR_SET_NAME, (unsigned long)"TableOfContentThread", 0, 0, 0);
    int t_pid = 0;
    int pri = getpriority(PRIO_PROCESS, t_pid);
    TOC_PRINT_DBG("TableOfContentThread::threadEntry ,priority = %d,pid=%d", pri, getpid());

    while (!mStopped)
    {
        Mutex::Autolock autoLock(mLock);

        if (useAFrameToTOC() != OK)
        {
            break;
        }
    }

    if (mStopped)
    {
        mEntryCount = 0;
        mTocComplete = false;
        TOC_PRINT_WARN("build TocTable process terminated ");
    }
    else
    {
        mTocComplete = true;
        mTocTimeUs = (mTocTimeUs + 500) / 1000 * 1000;
        mDuration = mTocTimeUs;
        sendDurationUpdateEvent(mDuration);
        TOC_PRINT_DBG("build TocTable process over--actual duration=%lld,mFrameNum=%d,mEntryCount=%d", (long long)mDuration, mFrameNum, mEntryCount);
    }

    return;
}

status_t TableOfContentThread::useAFrameToTOC()
{
    int64_t frameUs = 0;

    if (getNextFramePos(&mCurFilePos, &mNextFilePos, &frameUs) != OK)
    {
        return ERROR_END_OF_STREAM;
    }

    //TOC_PRINT_DBG("curPos=%d,nextPos=%d,framenum=%u,PerframeUs=%lld",mCurFilePos,mNextFilePos,mFrameNum,frameUs);
    --mTocIntervalLeft;

    if (0 == mTocIntervalLeft)
    {

        if (mEntryCount >= mTocSize)
        {
            for (int i = 0; (uint32_t)i < (mTocSize >> 1); i++)
            {
                //TOC_PRINT_DBG("m_Toc.mTocPos[%d]=%ld,m_Toc.mTocPos[%d]=%ld",i,m_Toc.TocPos[i],i<<1,m_Toc.TocPos[i<<1]);
                //TOC_PRINT_DBG("m_Toc.TocTS[%d]=%lld,m_Toc.TocTS[%d]=%lld",i,m_Toc.TocTS[i],i<<1,m_Toc.TocTS[i<<1]);
                m_Toc.TocPos.replaceAt(m_Toc.TocPos[i << 1], i);
                m_Toc.TocTS.replaceAt(m_Toc.TocTS[i << 1], i);
                m_Toc.PaktPos.replaceAt(m_Toc.PaktPos[i << 1], i);
            }

            mEntryCount = mTocSize >> 1;
            mTocInterval <<= 1;
            TOC_PRINT_DBG("mTocInterval=%u", mTocInterval);
        }

        if (m_Toc.size < mTocSize)
        {
            m_Toc.TocPos.push(mCurFilePos);
            m_Toc.TocTS.push(mTocTimeUs);
            m_Toc.PaktPos.push(mCurPaktPos);
            m_Toc.size++;
        }
        else
        {
            m_Toc.TocPos.replaceAt(mCurFilePos, mEntryCount);
            m_Toc.TocTS.replaceAt(mTocTimeUs, mEntryCount);
            m_Toc.PaktPos.replaceAt(mCurPaktPos, mEntryCount);
        }

        mEntryCount++;
        mTocIntervalLeft = mTocInterval;

    }

    mFrameNum++;
    mCurFilePos = mNextFilePos;
    mCurPaktPos = mNextPaktPos;
    mTocTimeUs += frameUs;
    return OK;
}

status_t TableOfContentThread::getFramePos(int64_t targetTimeUs, int64_t *pActualTimeUs, off64_t *pActualPos, bool bMandatory, bool bSekUseUndoneTable)
{
    Mutex::Autolock autoLock(mLock);
    TOC_PRINT_DBG("getFramePos--TargetTimeUs=%lld,tableTimeUs=%lld,bMandatory =%d", (long long)targetTimeUs, (long long)mTocTimeUs, bMandatory);
    TOC_PRINT_DBG("getFramePos--mTocInterval=%u,mEntryCount=%u", mTocInterval, mEntryCount);
    int     entry = 0;
    off64_t   curpos = 0;
    off64_t   nexpos = 0;
    int64_t nowTs = 0;
    int64_t nextTs = 0;
    int64_t frameUs = 0;

    off_t   curPaktPos = 0;
    off_t   nexPaktPos = 0;

    off64_t   prevPos = 0;
    int64_t prevTs = 0;
    off_t   prevPaktPos = 0;

    if (targetTimeUs > mTocTimeUs)
    {
        int64_t diffTimeUs = targetTimeUs - mTocTimeUs;

        if (bMandatory && !mTocComplete)
        {
            if (diffTimeUs < 480000000ll) //8 min
            {
                TOC_PRINT_DBG("Mandatory and little than 8 min");

                while (mTocTimeUs < targetTimeUs)
                {
                    if (useAFrameToTOC() != OK)
                    {
                        return ERROR_END_OF_STREAM;
                    }

                }
            }
            else
            {
                *pActualTimeUs  = m_Toc.TocTS[mEntryCount - 1];
                *pActualPos     = m_Toc.TocPos[mEntryCount - 1];
                mSeekPaktPos    = m_Toc.PaktPos[mEntryCount - 1];
                TOC_PRINT_DBG("difftime >8min seek::curpos=%lld,nowTs=%lld", (long long)*pActualPos, (long long)*pActualTimeUs);
                return BAD_VALUE;
            }
        }
        else
        {
            if (mEntryCount > 0)
            {
                *pActualTimeUs  = m_Toc.TocTS[mEntryCount - 1];
                *pActualPos     = m_Toc.TocPos[mEntryCount - 1];
                mSeekPaktPos    = m_Toc.PaktPos[mEntryCount - 1];
            }
            else  //when table has no items,we can't vector ,otherwise ne.
            {
                *pActualTimeUs  = 0;
                *pActualPos     = 0;
                mSeekPaktPos    = 0;
            }

            TOC_PRINT_WARN("SeekPoint is not in toc and Non Mandatory curpos=%lld,nowTs=%lld!", (long long)*pActualPos, (long long)*pActualTimeUs);
            return BAD_VALUE;
        }
    }

    off_t start = 0;
//    off_t end = mEntryCount - 1;
    off_t mid = 0;
    TOC_PRINT_DBG("mEntryCount =%u,start=%lld", mEntryCount, (long long)start);

    if (mEntryCount > 0)
    {
        off_t end = mEntryCount - 1;
        while (start <= end)
        {
            mid = (start + end) / 2;

            if (m_Toc.TocTS[mid] < targetTimeUs)
            {
                start = mid + 1;
            }
            else if (m_Toc.TocTS[mid] > targetTimeUs)
            {
                end = mid - 1;
            }
            else
            {
                *pActualTimeUs  = m_Toc.TocTS[mid];
                *pActualPos     = m_Toc.TocPos[mid];
                mSeekPaktPos    = m_Toc.PaktPos[mid];
                TOC_PRINT_DBG("seek::curpos=%lld,nowTs=%lld,paktPos=%lld", (long long)*pActualPos, (long long)*pActualTimeUs, (long long)mSeekPaktPos);
                return OK;
            }

            //LOGD("mid=%d,start=%d,end=%d",mid,start,end);
        }

        entry = end;
        curpos = m_Toc.TocPos[entry];
        nexpos = curpos;
        nowTs = m_Toc.TocTS[entry];
        nextTs = nowTs;
        curPaktPos = m_Toc.PaktPos[entry];
        nexPaktPos = curPaktPos;
    }
    else
    {
        *pActualTimeUs = 0;
        *pActualPos = 0;
        curPaktPos = 0;
        TOC_PRINT_DBG("@@ SeekTable has no item.Return Start Location!curpos=%lld,nowTs=%lld", (long long)*pActualPos, (long long)*pActualTimeUs);
        return OK;
    }

    TOC_PRINT_DBG("+seek::curpos=%lld,nowTs=%lld,paktPos=%lld,entry =%d", (long long)curpos, (long long)nowTs, (long long)curPaktPos, entry);
//#ifdef MTK_AOSP_ENHANCEMENT
    if (!mTocComplete && bSekUseUndoneTable)
    {
        *pActualTimeUs  = nowTs;
        *pActualPos     = curpos;
        mSeekPaktPos    = curPaktPos;
        TOC_PRINT_DBG("--seek::curpos=%lld,nowTs=%lld,paktPos=%lld", (long long)curpos, (long long)nowTs, (long long)mSeekPaktPos);
        return OK;
    }
//#endif
    while (nowTs < targetTimeUs)
    {
        prevPos = curpos;
        prevTs  = nowTs;
        prevPaktPos = mCurPaktPos;

        curpos = nexpos;
        nowTs  = nextTs;
        mCurPaktPos = curPaktPos;

        if (getNextFramePos(&curpos, &nexpos, &frameUs) != OK)
        {
            break;
            //TOC_PRINT_DBG("ERROR_END_OF_STREAM");
            //return ERROR_END_OF_STREAM;
        }

        nextTs += frameUs;
        frameUs = 0;
        curPaktPos = mNextPaktPos;
    }

    nowTs = prevTs;
    curpos = prevPos;
    mCurPaktPos = prevPaktPos;

    *pActualTimeUs  = nowTs;
    *pActualPos     = curpos;
    mSeekPaktPos    = mCurPaktPos;
    TOC_PRINT_DBG("-seek::curpos=%lld,nowTs=%lld,paktPos=%lld", (long long)curpos, (long long)nowTs, (long long)mSeekPaktPos);
    return OK;
}

}
