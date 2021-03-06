/***************************************************************************//**
* \file cy_crypto_core_aes_v1.c
* \version 2.20
*
* \brief
*  This file provides the source code fro the API for the AES method
*  in the Crypto driver.
*
********************************************************************************
* Copyright 2016-2019 Cypress Semiconductor Corporation
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/


#include "cy_crypto_core_aes_v1.h"

#include "cy_crypto_core_hw_v1.h"
#include "cy_crypto_core_mem_v1.h"
#include "cy_syslib.h"
#include <string.h>

#if defined(CY_IP_MXCRYPTO)

#if (CPUSS_CRYPTO_AES == 1)

static void Cy_Crypto_Core_V1_Aes_InvKey(CRYPTO_Type *base, cy_stc_crypto_aes_state_t const *aesState);

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_ProcessBlock
****************************************************************************//**
*
* Performs the AES block cipher.
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \param dirMode
* One of CRYPTO_ENCRYPT or CRYPTO_DECRYPT.
*
* \param dstBlock
* The pointer to the cipher text.
*
* \param srcBlock
* The pointer to the plain text. Must be 4-Byte aligned!
*
*******************************************************************************/
void Cy_Crypto_Core_V1_Aes_ProcessBlock(CRYPTO_Type *base,
                            cy_stc_crypto_aes_state_t const *aesState,
                            cy_en_crypto_dir_mode_t dirMode,
                            uint32_t *dstBlock,
                            uint32_t const *srcBlock)
{
    Cy_Crypto_SetReg3Instr(base,
                           (CY_CRYPTO_DECRYPT == dirMode) ? (uint32_t)aesState->invKey : (uint32_t)aesState->key,
                           (uint32_t)srcBlock,
                           (uint32_t)dstBlock);

    Cy_Crypto_Run3ParamInstr(base,
                            (CY_CRYPTO_DECRYPT == dirMode) ? CY_CRYPTO_V1_AES_BLOCK_INV_OPC : CY_CRYPTO_V1_AES_BLOCK_OPC,
                            CY_CRYPTO_RSRC0_SHIFT,
                            CY_CRYPTO_RSRC4_SHIFT,
                            CY_CRYPTO_RSRC12_SHIFT);

    /* Wait until the AES instruction is complete */
    while (0uL != _FLD2VAL(CRYPTO_STATUS_AES_BUSY, REG_CRYPTO_STATUS(base)))
    {
    }
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_Xor
****************************************************************************//**
*
* Perform the XOR of two 16-Byte memory structures.
* All addresses must be 4-Byte aligned!
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \param dstBlock
* The pointer to the memory structure with the XOR results.
*
* \param src0Block
* The pointer to the first memory structure. Must be 4-Byte aligned!
*
* \param src1Block
* The pointer to the second memory structure. Must be 4-Byte aligned!
*
*******************************************************************************/
void Cy_Crypto_Core_V1_Aes_Xor(CRYPTO_Type *base,
                            cy_stc_crypto_aes_state_t const *aesState,
                            uint32_t *dstBlock,
                            uint32_t const *src0Block,
                            uint32_t const *src1Block)
{
    Cy_Crypto_SetReg3Instr(base,
                           (uint32_t)src0Block,
                           (uint32_t)src1Block,
                           (uint32_t)dstBlock);

    /* Issue the AES_XOR instruction */
    Cy_Crypto_Run3ParamInstr(base,
                             CY_CRYPTO_V1_AES_XOR_OPC,
                             CY_CRYPTO_RSRC0_SHIFT,
                             CY_CRYPTO_RSRC4_SHIFT,
                             CY_CRYPTO_RSRC8_SHIFT);

    /* Wait until the AES instruction is complete */
    while(0uL != _FLD2VAL(CRYPTO_STATUS_AES_BUSY, REG_CRYPTO_STATUS(base)))
    {
    }
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_InvKey
****************************************************************************//**
*
* Calculates an inverse block cipher key from the block cipher key.
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
*******************************************************************************/
static void Cy_Crypto_Core_V1_Aes_InvKey(CRYPTO_Type *base, cy_stc_crypto_aes_state_t const *aesState)
{
    /* Issue the AES_KEY instruction to prepare the key for decrypt operation */
    Cy_Crypto_SetReg2Instr(base, (uint32_t)aesState->key, (uint32_t)aesState->invKey);

    Cy_Crypto_Run2ParamInstr(base,
                             CY_CRYPTO_V1_AES_KEY_OPC,
                             CY_CRYPTO_RSRC0_SHIFT,
                             CY_CRYPTO_RSRC8_SHIFT);

    /* Wait until the AES instruction is complete */
    while(0uL != _FLD2VAL(CRYPTO_STATUS_AES_BUSY, REG_CRYPTO_STATUS(base)))
    {
    }
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_Init
****************************************************************************//**
*
* Sets Aes mode and prepare inversed key.
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param key
* The pointer to the encryption/decryption key.
*
* \param keyLength
* \ref cy_en_crypto_aes_key_length_t
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \return
* A Crypto status \ref cy_en_crypto_status_t.
*
*******************************************************************************/
cy_en_crypto_status_t Cy_Crypto_Core_V1_Aes_Init(CRYPTO_Type *base,
                                                 uint8_t const *key,
                                                 cy_en_crypto_aes_key_length_t keyLength,
                                                 cy_stc_crypto_aes_state_t *aesState)
{
    aesState->keyLength = keyLength;

    /* Set the key mode: 128, 192 or 256 Bit */
    REG_CRYPTO_AES_CTL(base) = (uint32_t)(_VAL2FLD(CRYPTO_AES_CTL_KEY_SIZE, (uint32_t)(aesState->keyLength)));

    cy_stc_crypto_aes_buffers_t *aesBuffers = (cy_stc_crypto_aes_buffers_t *)(REG_CRYPTO_MEM_BUFF(base));

    aesState->buffers = (uint32_t*) aesBuffers;
    aesState->key     = (uint8_t *)(aesBuffers->key);
    aesState->invKey  = (uint8_t *)(aesBuffers->keyInv);

    Cy_Crypto_Core_V1_MemCpy(base, aesState->key, key, CY_CRYPTO_AES_256_KEY_SIZE);

    Cy_Crypto_Core_V1_Aes_InvKey(base, aesState);

    return (CY_CRYPTO_SUCCESS);
}

void Cy_Crypto_Core_V1_Aes_Free(CRYPTO_Type *base)
{
    Cy_Crypto_Core_V1_MemSet(base, REG_CRYPTO_MEM_BUFF(base), 0u, sizeof(cy_stc_crypto_aes_buffers_t));
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_Ecb
****************************************************************************//**
*
* Performs AES operation on one Block.
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param dirMode
* Can be \ref CY_CRYPTO_ENCRYPT or \ref CY_CRYPTO_DECRYPT
* (\ref cy_en_crypto_dir_mode_t).
*
* \param dst
* The pointer to a destination cipher block.
*
* \param src
* The pointer to a source block.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \return
* A Crypto status \ref cy_en_crypto_status_t.
*
*******************************************************************************/
cy_en_crypto_status_t Cy_Crypto_Core_V1_Aes_Ecb(CRYPTO_Type *base,
                                            cy_en_crypto_dir_mode_t dirMode,
                                            uint8_t *dst,
                                            uint8_t const *src,
                                            cy_stc_crypto_aes_state_t *aesState)
{
    cy_stc_crypto_aes_buffers_t *aesBuffers = (cy_stc_crypto_aes_buffers_t*)aesState->buffers;

    Cy_Crypto_Core_V1_MemCpy(base, &aesBuffers->block0, src, CY_CRYPTO_AES_BLOCK_SIZE);

    Cy_Crypto_Core_V1_Aes_ProcessBlock(base, aesState, dirMode, (uint32_t*)&aesBuffers->block1, (uint32_t*)&aesBuffers->block0);

    Cy_Crypto_Core_V1_MemCpy(base, dst, &aesBuffers->block1, CY_CRYPTO_AES_BLOCK_SIZE);

    return (CY_CRYPTO_SUCCESS);
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_Cbc
****************************************************************************//**
*
* Performs AES operation on a plain text with Cipher Block Chaining (CBC).
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param dirMode
* Can be \ref CY_CRYPTO_ENCRYPT or \ref CY_CRYPTO_DECRYPT
* (\ref cy_en_crypto_dir_mode_t)
*
* \param srcSize
* The size of the source plain text.
*
* \param ivPtr
* The pointer to the initial vector.
*
* \param dst
* The pointer to a destination cipher text.
*
* \param src
* The pointer to a source plain text.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \return
* A Crypto status \ref cy_en_crypto_status_t.
*
*******************************************************************************/
cy_en_crypto_status_t Cy_Crypto_Core_V1_Aes_Cbc(CRYPTO_Type *base,
                                            cy_en_crypto_dir_mode_t dirMode,
                                            uint32_t srcSize,
                                            uint8_t const *ivPtr,
                                            uint8_t *dst,
                                            uint8_t const *src,
                                            cy_stc_crypto_aes_state_t *aesState)
{
    uint32_t size = srcSize;

    cy_stc_crypto_aes_buffers_t *aesBuffers = (cy_stc_crypto_aes_buffers_t*)aesState->buffers;
    uint32_t *tempBuff = (uint32_t*)(&aesBuffers->iv);
    uint32_t *srcBuff  = (uint32_t*)(&aesBuffers->block0);
    uint32_t *dstBuff  = (uint32_t*)(&aesBuffers->block1);

    cy_en_crypto_status_t myResult = CY_CRYPTO_SIZE_NOT_X16;

    /* Check whether the data size is multiple of CY_CRYPTO_AES_BLOCK_SIZE */
    if (0uL == (uint32_t)(size & (CY_CRYPTO_AES_BLOCK_SIZE - 1u)))
    {
        /* Copy the Initialization Vector to the local buffer because it changes during calculation */
        Cy_Crypto_Core_V1_MemCpy(base, tempBuff, ivPtr, CY_CRYPTO_AES_BLOCK_SIZE);

        if (CY_CRYPTO_DECRYPT == dirMode)
        {
            while (size > 0uL)
            {
                /* source message block */
                Cy_Crypto_Core_V1_MemCpy(base, srcBuff, src, CY_CRYPTO_AES_BLOCK_SIZE);

                Cy_Crypto_Core_V1_Aes_ProcessBlock(base, aesState, dirMode, dstBuff, srcBuff);
                Cy_Crypto_Core_V1_Aes_Xor(base, aesState, dstBuff, tempBuff, dstBuff);

                /* temporary cipher block */
                Cy_Crypto_Core_V1_MemCpy(base, tempBuff, srcBuff, CY_CRYPTO_AES_BLOCK_SIZE);

                /* destination cipher text block */
                Cy_Crypto_Core_V1_MemCpy(base, dst, dstBuff, CY_CRYPTO_AES_BLOCK_SIZE);

                src  += CY_CRYPTO_AES_BLOCK_SIZE;
                dst  += CY_CRYPTO_AES_BLOCK_SIZE;
                size -= CY_CRYPTO_AES_BLOCK_SIZE;
            }
        }
        else
        {
            while (size > 0uL)
            {
                /* source message block */
                Cy_Crypto_Core_V1_MemCpy(base, srcBuff, src, CY_CRYPTO_AES_BLOCK_SIZE);

                Cy_Crypto_Core_V1_Aes_Xor(base, aesState, tempBuff, srcBuff, tempBuff);
                Cy_Crypto_Core_V1_Aes_ProcessBlock(base, aesState, dirMode, dstBuff, tempBuff);

                /* temporary cipher block */
                Cy_Crypto_Core_V1_MemCpy(base, tempBuff, dstBuff, CY_CRYPTO_AES_BLOCK_SIZE);

                /* destination cipher text block */
                Cy_Crypto_Core_V1_MemCpy(base, dst, dstBuff, CY_CRYPTO_AES_BLOCK_SIZE);

                src  += CY_CRYPTO_AES_BLOCK_SIZE;
                dst  += CY_CRYPTO_AES_BLOCK_SIZE;
                size -= CY_CRYPTO_AES_BLOCK_SIZE;
            }
        }

        myResult = CY_CRYPTO_SUCCESS;
    }

    return (myResult);
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_Cfb
********************************************************************************
*
* Performs AES operation on a plain text with the Cipher Feedback Block method (CFB).
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param dirMode
* Can be \ref CY_CRYPTO_ENCRYPT or \ref CY_CRYPTO_DECRYPT
* (\ref cy_en_crypto_dir_mode_t)
*
* \param srcSize
* The size of the source plain text.
*
* \param ivPtr
* The pointer to the initial vector.
*
* \param dst
* The pointer to a destination cipher text.
*
* \param src
* The pointer to a source plain text.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \return
* A Crypto status \ref cy_en_crypto_status_t.
*
*******************************************************************************/
cy_en_crypto_status_t Cy_Crypto_Core_V1_Aes_Cfb(CRYPTO_Type *base,
                                             cy_en_crypto_dir_mode_t dirMode,
                                             uint32_t srcSize,
                                             uint8_t const *ivPtr,
                                             uint8_t *dst,
                                             uint8_t const *src,
                                             cy_stc_crypto_aes_state_t *aesState)
{
    uint32_t size = srcSize;
    cy_en_crypto_status_t myResult = CY_CRYPTO_SIZE_NOT_X16;

    cy_stc_crypto_aes_buffers_t *aesBuffers = (cy_stc_crypto_aes_buffers_t*)aesState->buffers;

    uint32_t *srcBuff = (uint32_t*)(aesBuffers->block0);
    uint32_t *dstBuff = (uint32_t*)(aesBuffers->block1);

    uint32_t *encBuff = dstBuff;   /* Default operation is ENCRYPT */

    /* Check whether the data size is multiple of CY_CRYPTO_AES_BLOCK_SIZE */
    if (0uL == (size & (CY_CRYPTO_AES_BLOCK_SIZE - 1u)))
    {
        if (CY_CRYPTO_DECRYPT == dirMode)
        {
            encBuff = srcBuff;
        }

        /* Copy the Initialization Vector to local encode buffer */
        Cy_Crypto_Core_V1_MemCpy(base, encBuff, ivPtr, CY_CRYPTO_AES_BLOCK_SIZE);

        while (size > 0uL)
        {
            /* In this mode, (CFB) is always an encryption! */
            Cy_Crypto_Core_V1_Aes_ProcessBlock(base, aesState, CY_CRYPTO_ENCRYPT, dstBuff, encBuff);

            /* source message block */
            Cy_Crypto_Core_V1_MemCpy(base, srcBuff, src, CY_CRYPTO_AES_BLOCK_SIZE);

            Cy_Crypto_Core_V1_Aes_Xor(base, aesState, dstBuff, srcBuff, dstBuff);

            /* destination cipher text block */
            Cy_Crypto_Core_V1_MemCpy(base, dst, dstBuff, CY_CRYPTO_AES_BLOCK_SIZE);

            src  += CY_CRYPTO_AES_BLOCK_SIZE;
            dst  += CY_CRYPTO_AES_BLOCK_SIZE;

            size -= CY_CRYPTO_AES_BLOCK_SIZE;
        }

        myResult = CY_CRYPTO_SUCCESS;
    }

    return (myResult);
}

/*******************************************************************************
* Function Name: Cy_Crypto_Core_V1_Aes_Ctr
********************************************************************************
*
* Performs AES operation on a plain text using the counter method (CTR).
*
* \param base
* The pointer to the CRYPTO instance address.
*
* \param srcSize
* The size of a source plain text.
*
* \param srcOffset
* The size of an offset within the current block stream for resuming within the
* current cipher stream.
*
* \param ivPtr
* The 128-bit nonce and counter.
*
* \param streamBlock
* The saved stream-block for resuming. Is over-written by the function.
*
* \param dst
* The pointer to a destination cipher text.
*
* \param src
* The pointer to a source plain text. Must be 4-Byte aligned.
*
* \param aesState
* The pointer to the aesState structure which stores the AES context.
*
* \return
* A Crypto status \ref cy_en_crypto_status_t.
*
*******************************************************************************/
#define CY_CRYPTO_AES_CTR_CNT_POS          (0x02u)
cy_en_crypto_status_t Cy_Crypto_Core_V1_Aes_Ctr(CRYPTO_Type *base,
                                            uint32_t srcSize,
                                            uint32_t *srcOffset,
                                            uint8_t  *ivPtr,
                                            uint8_t  *streamBlock,
                                            uint8_t  *dst,
                                            uint8_t  const *src,
                                            cy_stc_crypto_aes_state_t *aesState)
{
    uint32_t cnt;
    uint32_t i;
    uint64_t counter;

    cy_stc_crypto_aes_buffers_t *aesBuffers = (cy_stc_crypto_aes_buffers_t*)aesState->buffers;
    uint32_t *nonceCounter = (uint32_t*)(&aesBuffers->iv);
    uint32_t *srcBuff      = (uint32_t*)(&aesBuffers->block0);
    uint32_t *dstBuff      = (uint32_t*)(&aesBuffers->block1);
    uint32_t *streamBuff   = (uint32_t*)(&aesBuffers->block2);

    Cy_Crypto_Core_V1_MemCpy(base, nonceCounter, ivPtr, CY_CRYPTO_AES_BLOCK_SIZE);

    counter = CY_SWAP_ENDIAN64(*(uint64_t*)(nonceCounter + CY_CRYPTO_AES_CTR_CNT_POS));

    cnt = (uint32_t)(srcSize / CY_CRYPTO_AES_BLOCK_SIZE);

    for (i = 0uL; i < cnt; i++)
    {
        /* source message block */
        Cy_Crypto_Core_V1_MemCpy(base, srcBuff, src, CY_CRYPTO_AES_BLOCK_SIZE);

        /* In this mode, (CTR) is always an encryption! */
        Cy_Crypto_Core_V1_Aes_ProcessBlock(base, aesState, CY_CRYPTO_ENCRYPT, streamBuff, nonceCounter);

        /* Increment the nonce counter, at least 64Bits (from 128) is the counter part */
        counter++;
        *(uint64_t*)(nonceCounter + CY_CRYPTO_AES_CTR_CNT_POS) = CY_SWAP_ENDIAN64(counter);

        Cy_Crypto_Core_V1_Aes_Xor(base, aesState, dstBuff, srcBuff, streamBuff);

        /* destination cipher text block */
        Cy_Crypto_Core_V1_MemCpy(base, dst, dstBuff, CY_CRYPTO_AES_BLOCK_SIZE);

        src += CY_CRYPTO_AES_BLOCK_SIZE;
        dst += CY_CRYPTO_AES_BLOCK_SIZE;
    }

    Cy_Crypto_Core_V1_MemCpy(base, ivPtr, nonceCounter, CY_CRYPTO_AES_BLOCK_SIZE);

    /* Save the reminder of the last non-complete block */
    *srcOffset = (uint32_t)(srcSize % CY_CRYPTO_AES_BLOCK_SIZE);

    return (CY_CRYPTO_SUCCESS);
}

#endif /* #if (CPUSS_CRYPTO_AES == 1) */

#endif /* CY_IP_MXCRYPTO */


/* [] END OF FILE */
