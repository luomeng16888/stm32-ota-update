/**
  ******************************************************************************
  * @file    network_data_params.c
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-05-17T22:00:24+0800
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
  0x7f71bf553d81e598U, 0x9ec381d904ccfb58U, 0x1fae507f7abdf281U, 0x84f1814925743502U,
  0x9ea945a881031a81U, 0xfffff69afffff4cfU, 0xfffff1f20000037cU, 0x325b00001247U,
  0xf1affffe6f9U, 0xbd3378811b37e6b8U, 0x3cff5498523a61c5U, 0x56dac7fb312bdc06U,
  0xe5b821bc0dda1718U, 0xdf033d8113314e12U, 0x35121701ec3437e1U, 0xf4d67fdd27ccf6f2U,
  0x7ef73be0c2e5afeU, 0xdb2f5c08d9df3b09U, 0xc6ae26f431411ad5U, 0x31ec55b70e2311deU,
  0x44144f201cc27f8fU, 0x3b0cfe913ee88123U, 0xe60521381eedcbfbU, 0xaf903441bda72419U,
  0xc73c7f4770fc2b27U, 0xeb19704ccee4d5afU, 0x3a324a6940ef3f17U, 0x29232ffdd90517c7U,
  0xf305fbbc0c2b0c09U, 0x12218d826d77f3fU, 0xf7e87fdd1b101206U, 0x12de54e703ee47ddU,
  0xeb055beae9fe0bdbU, 0x237f2626e22036U, 0x10de503a1cd6db30U, 0xebea58391af63c11U,
  0xc0d7d517cde71e92U, 0xf78105faa30abd15U, 0x26ef7e24b8044cb3U, 0xd93a0bccdc050ec7U,
  0x26322cc9324c2223U, 0xbafeca7fd0f6af98U, 0xeadb7fd1eddf0118U, 0x1423993f5d5ebf0U,
  0x2ee03d30203169fcU, 0xde244389f4e7d311U, 0xf829fcc13b183916U, 0x3e0e131fda047feeU,
  0x1f55ebfcde2dad7U, 0x11c75181dc1641ccU, 0x25f933bef5dbe02dU, 0xae3dfb022d30418U,
  0xca1ad39f02c44215U, 0x22130d7fdf311dd3U, 0x181b14f2dcfe18dfU, 0xef35db091f60ef68U,
  0x8ddddc2cd1815baeU, 0x47cfffffed5U, 0x2d100000b9aU, 0x47500000ab7U,
  0xac3fffffeffU, 0x1a9f000008f9U, 0x2e3000000c7U, 0xd4cfffffbc1U,
  0xd7fffff9c6U, 0xfbf922d308cc0707U, 0x880081f2ebfcfa25U, 0x2504fd15f8070a08U,
  0xf9fd28030001da15U, 0xf8f6eee8fde9f3f8U, 0xb3f32001fff12ac9U, 0xb4980a1a0a25488fU,
  0xf0f4f37a4c4c8e6U, 0x8ff6decc065c42b2U, 0x843969fb2c64c9b9U, 0x69f6781c3bd0549U,
  0x401139ef4f22dbe1U, 0xdff5f8bee30ef7eeU, 0xe720b512e6fc81d0U, 0xe01a253b12a7e1e7U,
  0xb5f6ebdfc3f92115U, 0x25d91c52d8f09518U, 0xa9d0e906e2ec1138U, 0x270afecf230aff11U,
  0xeef75de409d3640fU, 0x43ecea120720230aU, 0x7f444cee10014620U, 0xbe6f8d8f70800e7U,
  0x641431f4ed21bc0aU, 0xf714fce127a31af8U, 0xbefeb002e51cd3d4U, 0xbe1d21cebce1ee7U,
  0xcdf8071ee72843f5U, 0x1d0c1532fd0ddedeU, 0xb6001b0c26ee7f18U, 0x4d2426322525e2e8U,
  0x4a162bf1f8e83104U, 0x30d0be1240bd4d2U, 0xe6f22fd91dfe7f19U, 0x2211fc18de0e0d22U,
  0x9218192207f8ea03U, 0x200b1d30fc1af804U, 0xf312f8f80016ea1fU, 0xff2ff290b0ec6e8U,
  0x89d4efe5f4bf1611U, 0xde406ff0f0ef9feU, 0x8115f1fbf4ef0508U, 0x34f5ead810240a05U,
  0xde0653e316f77f10U, 0x1d17e9cd0843f9fbU, 0x21e2611c17e54ef9U, 0x1a1dfacd01310815U,
  0xff6e1b15ded416U, 0x21df12ebf9030218U, 0xd5f867ec1ce27ff1U, 0x121017d6ed2c1e06U,
  0x7c0c650803fe53ffU, 0x1de81bf01929120aU, 0x2a175bf11c23c3f9U, 0xa0af8f7d703dce5U,
  0xf20c81e7e414982eU, 0x50e08fb17320fcfU, 0x74da7af5f0148e08U, 0x406d137021513ecU,
  0x5b275001ec0785f8U, 0x2d0406d30f2417ebU, 0x1b0c53fcf7087f16U, 0x181012f30026020bU,
  0x52f53504f9016015U, 0xf7ee13f90a1aff0bU, 0x3b0e510419ffcfeeU, 0x1fea0cfdf0ffeef1U,
  0x8f021e0f904d41aU, 0xd605fd2a03111515U, 0xb90a1dee16fe6deeU, 0x8107e634e9abd50aU,
  0x590d9bd4e03bf001U, 0xfe910140bf207f4U, 0x29f719fe10078517U, 0x713f3f30ee315fcU,
  0xd001280f15f981f2U, 0x7f7f7fe09f61312U, 0x951122000bf23cefU, 0xfb18f2e90503f7fdU,
  0x5ced670a10d1f308U, 0xecef00e5f1160c14U, 0x4802c603d92d0ce2U, 0xe1eaf8ead6effb05U,
  0x7f141fd00126e506U, 0x18f8e8d20d11110fU, 0xfcec4b0903ee740fU, 0x2304fbf40f3900efU,
  0x5f073f0d1aed7f19U, 0x2318f0e5fa12f0ffU, 0x5aee34f51107e706U, 0x1e28f68f2043e70bU,
  0xb1cc410ac1817b03U, 0x6d11eb6e15de1e2U, 0x3801262045f8702cU, 0x8e105e0c515f90bU,
  0xf8f2480d1b11161bU, 0xfffffd8800000039U, 0x37a00000542U, 0xfffffea9ffffff82U,
  0x13700000730U, 0xfffffd7e00000138U, 0x11400000065U, 0xfffffc64ffffff84U,
  0x6ce00000035U, 0x277ff325e2eb2c7cU, 0xf9c33f87b0c2b6U, 0x57f183cc32bc93cU,
  0xe5d0fa32d3e2bedeU, 0xd17f033b09fc202fU, 0x1ebcf00f14c6bb08U, 0xc77f1b11004e362cU,
  0xf2ef2c3c17baa9d1U, 0xde7fe83f0e4b1411U, 0xadea341021e98dc0U, 0xb87f0847aa6bd200U,
  0xf8c833f46b00f428U, 0xd663e323dd0d02bbU, 0xefde6287fffd7c2U, 0xdd391026f12dd4d2U,
  0xe5071ed47fe4f3ecU, 0xfffffeb9fffffd8eU, 0x3000000227U, 0xfffffc6afffffeebU,
  0xfffffe4bfffffd9bU,
};


ai_handle g_network_weights_table[1 + 2] = {
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
  AI_HANDLE_PTR(s_network_weights_array_u64),
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
};

