/**
 *  Copyright (c) 2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "GMSubtitlePlayer.h"
#include "GMPlayer.h"

fsl_osal_ptr GMSubtitlePlayer::ThreadFunc(fsl_osal_ptr arg)
{
    GMSubtitlePlayer *subPlayer = (GMSubtitlePlayer*)arg;

    while(OMX_TRUE != subPlayer->isStoped() 
            && OMX_ErrorNone == subPlayer->ThreadHandler());

    return NULL;
}

GMSubtitlePlayer::GMSubtitlePlayer(GMPlayer *player)
{
    mAVPlayer = player;
    pThread = NULL;
    pSem = NULL;
    bThreadStop = OMX_FALSE;
    bReset = OMX_FALSE;
}

OMX_ERRORTYPE GMSubtitlePlayer::Init()
{
    if(fsl_osal_sem_init(&pSem, 0, 0) != E_FSL_OSAL_SUCCESS) {
        LOG_ERROR("Failed to create semphore for subtitle thread.\n");
        return OMX_ErrorInsufficientResources;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_cond_create(&Cond)) {
        LOG_ERROR("Create condition variable for subtitle thread.\n");
        return OMX_ErrorInsufficientResources;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_thread_create(&pThread, NULL, GMSubtitlePlayer::ThreadFunc, this)) {
        LOG_ERROR("Failed to create subtitle thread.\n");
        return OMX_ErrorInsufficientResources;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMSubtitlePlayer::DeInit()
{
    if(pThread) {
        fsl_osal_thread_destroy(pThread);
        pThread = NULL;
    }

    if(Cond != NULL) {
        fsl_osal_cond_destroy(Cond);
        Cond = NULL;
    }

    if(pSem) {
        fsl_osal_sem_destroy(pSem);
        pSem = NULL;
    }

    return OMX_ErrorNone;
}


OMX_ERRORTYPE GMSubtitlePlayer::SetSource(
        GMSubtitleSource *source)
{
    LOG_DEBUG("GMSubtitlePlayer SetSource %p\n", source);
    mSource = source;
    return OMX_ErrorNone;
}


OMX_ERRORTYPE GMSubtitlePlayer::Start()
{
    LOG_DEBUG("GMSubtitlePlayer::Start\n");
    bThreadStop = OMX_FALSE;
    bReset = OMX_FALSE;
    fsl_osal_sem_post(pSem);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMSubtitlePlayer::Stop()
{
    LOG_DEBUG("GMSubtitlePlayer::Stop\n");
    bThreadStop = OMX_TRUE;
    fsl_osal_cond_broadcast(Cond);
    fsl_osal_sem_post(pSem);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMSubtitlePlayer::Reset()
{
    LOG_DEBUG("GMSubtitlePlayer::Reset\n");

    bReset = OMX_TRUE;
    while(fsl_osal_sem_trywait(pSem) == E_FSL_OSAL_SUCCESS);
    fsl_osal_cond_broadcast(Cond);

    mSample.pBuffer = NULL;
    RenderOneSample();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMSubtitlePlayer::ThreadHandler()
{
    fsl_osal_sem_wait(pSem);

    while(bThreadStop != OMX_TRUE) {
        GM_STATE State = GM_STATE_NULL;
        mAVPlayer->GetState(&State);
        if(State == GM_STATE_PAUSE) {
            fsl_osal_sleep(500000);
            continue;
        }

        OMX_BOOL bEOS = OMX_FALSE;
        mSource->GetNextSubtitleSample(mSample, bEOS);
        if(bEOS == OMX_TRUE)
            break;
        LOG_DEBUG("subtitle: format: %d, buffer: %s, len: %d, start pts: %lld\n",
                mSample.fmt, mSample.pBuffer, mSample.nFilledLen, mSample.nStartTime);

        OMX_TICKS nMediaTime = 0;
        mAVPlayer->GetMediaPositionNoLock(&nMediaTime);
        OMX_S32 diff = mSample.nStartTime - nMediaTime;

        LOG_DEBUG("diff: %d, start time: %lld, media time: %lld\n",
                diff, mSample.nStartTime, nMediaTime);

        if(diff > 0) {
            fsl_osal_cond_timedwait(Cond, diff);
        }

        if(bThreadStop == OMX_TRUE) {
            LOG_DEBUG("GMSubtitlePlayer break as stop.\n");
            return OMX_ErrorUndefined;
        }

        if(bReset == OMX_TRUE) {
            LOG_DEBUG("GMSubtitlePlayer break as Reset.\n");
            return OMX_ErrorNone;
        }

        RenderOneSample();
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE GMSubtitlePlayer::RenderOneSample()
{
    GM_SUBTITLE_SAMPLE sGMSample;

    sGMSample.pBuffer = mSample.pBuffer;
    sGMSample.nLen = mSample.nFilledLen;
    sGMSample.nTime = mSample.nStartTime;
    if(mSample.fmt == GMSubtitleSource::TEXT)
        sGMSample.fmt = SUBTITLE_TEXT;
    else if(mSample.fmt == GMSubtitleSource::BITMAP)
        sGMSample.fmt = SUBTITLE_BITMAP;
    else
        sGMSample.fmt = SUBTITLE_UNKNOWN;

    mAVPlayer->RenderSubtitle(sGMSample);

    return OMX_ErrorNone;
}

