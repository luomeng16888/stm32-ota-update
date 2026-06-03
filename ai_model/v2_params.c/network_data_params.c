/**
  ******************************************************************************
  * @file    network_data_params.c
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-05-17T21:55:15+0800
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

#include "network_data_params.h"


/**  Activations Section  ****************************************************/
ai_handle g_network_activations_table[1 + 2] = {
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
  AI_HANDLE_PTR(NULL),
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
};




/**  Weights Section  ********************************************************/
AI_ALIGNED(32)
const ai_u64 s_network_weights_array_u64[189] = {
  0x187f4f28ba478185U, 0xe8ae43647ff49b1bU, 0x8571e37fbb4eb881U, 0xf77f7f4df08f50f0U,
  0x7f7840baceac5cf5U, 0xfffffa8200000208U, 0xdeffffff0d3U, 0xfffff0320000361dU,
  0x139a00002726U, 0xf1fcace0ceea01bdU, 0x17fdb25a9ff07ebU, 0x6133e4300c52e2d0U,
  0xeaf20405cc587fa0U, 0x152c23d1e337054bU, 0x4cb9589bec5d5304U, 0xe6f9ef3c39def9ddU,
  0xc6d381c33ac7c63aU, 0x9437c4e1eebc14d3U, 0x24f05806cef0db54U, 0x1e3fa585d0b71ffU,
  0x810fb72e55eacdcdU, 0xb81e49bf57aedc54U, 0xcb102531554b6c64U, 0xdb7f4c0ecdc4ed15U,
  0x40de3ccd17eb43d6U, 0xf3e335db19b535dbU, 0xf381b297f92812eaU, 0x3a7f3ae6461a0f27U,
  0xa95ad7ee4538f3e5U, 0x5d506fc52083bc2U, 0x2805d2da19c58131U, 0x3135f0ebcdecbdd2U,
  0x3a240edfdc06d800U, 0xee080346011af0d0U, 0xd03dea2ffe20e118U, 0x187feb1fcf32e7c7U,
  0x2dd05088acd3ab01U, 0xdac8106d46822baU, 0x931d5e67efa44415U, 0xd8f2815f29bbcf6aU,
  0x5428fc4f6265c5d6U, 0xdc0341c6de2e18d2U, 0xc766b0faf119ecc9U, 0xd63831edf5ffdb13U,
  0xc7fe01116084831U, 0x3e54d0b9de4fd6cdU, 0xe34e57b7c2213e3eU, 0x6181e2e1af2b021eU,
  0x29adb97f601727ebU, 0x9d27c80b5670e038U, 0xca0d006375d91cd8U, 0x4e80dd833e037efU,
  0x3e597fea11d95c4bU, 0x3bd13ba54ff11c3dU, 0x20bf7f98303246d4U, 0xe5ab9e5c6435bd0U,
  0x2f1267a33ca81af9U, 0x5e00000ba1U, 0x90cfffffd5aU, 0x18600000dfbU,
  0x57900000a89U, 0x10b60000167fU, 0x75100001008U, 0xc8ffffffb28U,
  0xfffffb8c000002daU, 0x582c071a0301a408U, 0xbb2610cdf328db0eU, 0x7fface17e5c6d504U,
  0xa4e7dfb5f4fcba1eU, 0x58f1e4e3e0ddec19U, 0xebd3c7d0bfffeb0cU, 0xdfd31914ddd30644U,
  0x12513f20e3f0d7fU, 0xdff4360d45e163f9U, 0x322619ee3adcf366U, 0xb5f4fe3d46ec19ceU,
  0x123ee3e017eeee21U, 0x9b42161b26b9d859U, 0x2c7b148a5c81d95U, 0x5594e7e22304d9fdU,
  0xa21fe62a182b3934U, 0x1984c7b050caffb3U, 0xcf9dc7f381f2de25U, 0xca4932b0f048bf11U,
  0xe1f516fe02aad273U, 0x8acc02bb49472c13U, 0x9a3b222e5221b37fU, 0x2504e8bc34c713b5U,
  0xef0ce12f16dbf70eU, 0xe01a04032913ff5fU, 0x2ee11136ee1f2170U, 0x81f84ce4f724035cU,
  0x26e20011e41e665dU, 0x8739632b0133f834U, 0x33fa2a3910ed1063U, 0xb0e306fdef1c0716U,
  0xebe9f91311e90e25U, 0xf1fb5320efe5f029U, 0x3917313b1f2b4161U, 0x81285f36e4eb090dU,
  0x19e6dff6e1f82763U, 0xeb53660ee3ab67e0U, 0x1bcde80d9372720cU, 0x4cd4cd8f5a4442eU,
  0x25f1b9a22cec81c0U, 0x9e5d72ea97deeadaU, 0xc23098014895ca54U, 0x1a3031eaed1c2d36U,
  0xc6063916232e0d19U, 0xc2016640f2182106U, 0x5135fb5ad930087fU, 0x822e0c3524044049U,
  0xfd9080ce6cf5775U, 0x2169b017a8a9cd09U, 0x25c50608b0ec901eU, 0x15a8cbe297e3a5c8U,
  0x61a0811be4a3e190U, 0x871b7002fa710030U, 0x3334fb6f05a3bb6U, 0xce0c080f17e3ef53U,
  0x33e3d143ecf61afeU, 0xbe2e3dc9de443b10U, 0x1be8144f1b3d317fU, 0xb63516214534fc3eU,
  0xfb0725ed4934f02bU, 0xf435f9180508e556U, 0xd72be515400134U, 0xa62d22183d152afcU,
  0x59e83e45120c5c7fU, 0xa12b77dce2e8dbe1U, 0x30e3e00718d54c66U, 0x7ff6be2729feb764U,
  0xc53adbcd01a9235fU, 0x130b19acb8fda51cU, 0xa0f52df6fd22d006U, 0xbbbc1fd6f50bc7aaU,
  0x25d2e13ed4e9decfU, 0x8ff2e8e59342fb2fU, 0xde44a84f32d4e097U, 0xd6b92bc84dc941ecU,
  0xc5f625589b452881U, 0xb7ca4725113caf58U, 0xc0450c8cf6870212U, 0x86002c1eedd61a30U,
  0xf10815eb1525f97fU, 0xe3df5d0108fa0b56U, 0x5537f807f22c1b76U, 0x9b3060f027ed21eeU,
  0x3fcdeea0505e659U, 0xdf0026f8e1eff40bU, 0xd71e12fd1a0e1c1cU, 0x96ff190116e72239U,
  0xee3223d0dfa202fU, 0x811514e8f2f20437U, 0x2ef8fdfc1404f122U, 0xda032e233ae0ef1aU,
  0xd81c340bfc30d573U, 0xba301d1527ce4717U, 0x7735e228e13a307fU, 0xe0e742061f2ff5f9U,
  0x141cf43b3d02f677U, 0xb5e00001191U, 0x5defffff7c5U, 0xa4800000ca4U,
  0xd08fffffe6dU, 0xae7fffff807U, 0xfffff90900000e5aU, 0xde8fffff426U,
  0xa22000008deU, 0xee23bdcbd0e1217fU, 0xfacb00f7d003c2edU, 0xc03db8f11844247fU,
  0x3aa2b23f9c5ccf6U, 0x1aa911e63bdef87fU, 0xbfe1a5c54929b30dU, 0x162408a9ec0ac57fU,
  0xbfbdec4033352829U, 0xb8a0d030cd4ed27fU, 0x2ccae1f94dc5b13bU, 0x1296a9f5ad269c7fU,
  0xe6991530c7c71db3U, 0x1ff3c8e3ebc2e97fU, 0xb730f01743f4c9c0U, 0xb3021c2223dc896aU,
  0x22428f2a2d815855U, 0xfffff85ffffffa5eU, 0xfffff7f9fffff841U, 0xfffff6d9fffff8e0U,
  0xfffff18efffff72cU,
};


ai_handle g_network_weights_table[1 + 2] = {
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
  AI_HANDLE_PTR(s_network_weights_array_u64),
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
};

