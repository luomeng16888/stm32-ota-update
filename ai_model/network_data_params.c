/**
  ******************************************************************************
  * @file    network_data_params.c
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-05-17T20:06:04+0800
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
  0x811ab91c0b069d81U, 0x39d68159552db2baU, 0xa781e44ee2d15b7fU, 0x81a83c7f1ecb46b2U,
  0x3c7f4438f22d5891U, 0x24d000008f7U, 0x5ae00000fe1U, 0xffffff9700000ad7U,
  0xfffffcaa0000110dU, 0x75f428028195ac81U, 0x3ba7ff9e95843897U, 0x42ad0e0df983076cU,
  0xd5da478d94c42233U, 0x7d4890c248ff69c8U, 0x12931248467f341cU, 0xb2ad451d937f0f4dU,
  0x4d70d5eee9cb362bU, 0xf4efd1e9e00254a0U, 0x8b4ab781ccf981d3U, 0xd28b5a41f2109434U,
  0x438a28cfd9531d4fU, 0xf688081b819c23a5U, 0x9018c99ce126ed4bU, 0x6cfe9342a6da3eb0U,
  0x7ff371dd170408dbU, 0x2fca3ff72911d449U, 0x72530d7b3ce1c5e6U, 0xd77f5656c465e2fbU,
  0xb44adaa15b76a5a5U, 0x6fbb48ddba034b10U, 0x59dd0b1eee26372fU, 0xf3b2b9ae2536a753U,
  0x54fdf8d5ce32977fU, 0x395acf4cff621805U, 0xc535cd5f19e4d9d5U, 0x5d62df71c67fe979U,
  0xf708be1228cac96bU, 0xb29fe8acaa3f635fU, 0x18a08130123710f7U, 0xe0025ae4a7a904cdU,
  0xb2f5a84aa67f33c9U, 0xbcf8caad116c87d6U, 0x95a5adc6812657ebU, 0x190d64e04db447e9U,
  0x101315f72e6e0c44U, 0x21435473f43453f2U, 0x2a2b17d87fc60d4eU, 0x24c468671d2d3535U,
  0x360f9687351fffb1U, 0x81301650c2b525b1U, 0xd441ecf75fe5fc37U, 0x255deff496efcb05U,
  0x38c23afb61dbf9ddU, 0x1fcc8844c97f4aafU, 0xf4c204b03f19135bU, 0x5ed67ff7d2495ae5U,
  0xbb34c12f695e2202U, 0xfffff8affffffd19U, 0xffffef060000227cU, 0x222bfffff4c1U,
  0x15330000286dU, 0x109b00001823U, 0xfffff115fffffb9bU, 0xfffff29e000013ecU,
  0x104000001213U, 0xdc99411ebdad8a5eU, 0x33dbdb30c5b939cU, 0xf653acfcb3f35c17U,
  0x4363faf781760ea6U, 0xb59ef3eaddd2b6cbU, 0xfbf91241673ccfedU, 0x1036b027f623a28U,
  0xcc485d1c0af51b01U, 0x2967743bdf4b3a2dU, 0xe4709fd90e2b4adbU, 0x643110b8cd73bcdbU,
  0xcd1d2d165e042ac9U, 0x347d4e1d410e8b7U, 0xdb035aa85c39570fU, 0xc87fa946edabf268U,
  0x30341c5d6cfce2daU, 0x153c203501592da5U, 0x653b4deb4e522b0dU, 0x3cedd2d22bcfd6aeU,
  0x2bf1813d3ca5ab0eU, 0x35da56164a27a93dU, 0xbac1ebe49905f2e2U, 0x5db3dec583dd4b9U,
  0x8bb9bd0ed1c73518U, 0x6f149cfafdfbed2cU, 0xfa3f9fea00424a7fU, 0xb425394fa7a9db05U,
  0x65de29f6cd94daa6U, 0xaa668fedb9e049dcU, 0x9b5b0d6056642fU, 0x3819edfd3a33d5d8U,
  0xc9fbd3e3a3e61a9bU, 0xb5814809b50ec4e1U, 0xacbe09242700bf4eU, 0x1e90ffb6cb0eed28U,
  0xd854fcd9adbe83fU, 0x3f4a7be33b43af2cU, 0x363428c70194ebb7U, 0x4bffc648e8c49cd2U,
  0x3f34bf5e41da9ca6U, 0x461d31fca433b965U, 0x8153605c8e72edb7U, 0x4cebeb6242c4341dU,
  0xb173c4ef7c6eaabeU, 0x667bfbab7e4bddecU, 0x10484f4b7fec05f9U, 0xc7e00f7ca0f1673U,
  0xc562121d0ed85c70U, 0x74b7b3011c15b02U, 0xe9fb517553d80a18U, 0x6e605112bd6af8bbU,
  0x6e6bde79b9df144aU, 0x385422aecc7fd02bU, 0xe136ccbbcabcef5dU, 0xd6ba8b21e4eb15e4U,
  0xfd71428f9e57e519U, 0xb63268b7ad70ae09U, 0x2ab0b7cfcd49e7e1U, 0xdf6ab5d16ce5013fU,
  0xa37ef3ced94581fdU, 0x7014672e277b3f9bU, 0x15decbd154f55077U, 0xd3d835f29f65dbd3U,
  0x47030331365198c5U, 0x22aed1ac7fcff148U, 0xee6f335b5217ffU, 0x2b3f4264c00d49edU,
  0x3a7fc2e72c1d6611U, 0xdb4907df1f5fc1f6U, 0x37fe015a5ef9b4bdU, 0xe4e0829b2250af8U,
  0x4778d9ce20934d3bU, 0xae25fdbcd4982599U, 0x82c035664cc4e6e5U, 0xdd65bb448d3766ccU,
  0x6a2293b729c86542U, 0x5feb99200d548141U, 0x1410ea4003dfebd1U, 0xcecfc342ea5795ceU,
  0x96f4fce22c6f49d4U, 0xaae7037514c1ed8dU, 0x111f032f10f1ccdcU, 0x7236e421fa3c8155U,
  0x924751960db3892fU, 0x3aa5bbabd3e224e2U, 0x93ed37929bd7f8f0U, 0x88ccb7a2cee9fbb1U,
  0xd9f0af3eb39d939U, 0x1681bc39f767c0deU, 0xb2ba1b53f22d4614U, 0xeb5e69a50c44aac7U,
  0x7bca733b8d44644U, 0x4804b4f4b3cd2a0eU, 0x7036e16a5d5ef1f7U, 0xb36f9ec112418c3U,
  0xf826eb75c8e27ffbU, 0x1cabfffff824U, 0xfffff5f5000008faU, 0xfffff95d00001497U,
  0xeaefffff736U, 0xfffff74f00001359U, 0xfc300000d9bU, 0xfffff891fffff323U,
  0x10a7fffffa71U, 0x4f4278335f4b3122U, 0x99f7132281516cb1U, 0x915600e8babda664U,
  0x97c47a7736957ffbU, 0xbd520ea427cdad01U, 0xc7d781867a399781U, 0x9ab1eb01f6cc042U,
  0x21418dcf819c1ac5U, 0xcb0ca6be88fc5444U, 0xc27f0c2b842b48caU, 0xe922e1d5bf92e8afU,
  0x35dc54ed27eb2881U, 0xce62719bedaadd26U, 0xb87f64284cb433dcU, 0x8f3d7fa0eee3b11aU,
  0x8d7ff43bc6c36a31U, 0xfffff1f8fffff46cU, 0xfffff3cffffff024U, 0xfffff3d6fffff75aU,
  0xfffff867fffff8acU,
};


ai_handle g_network_weights_table[1 + 2] = {
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
  AI_HANDLE_PTR(s_network_weights_array_u64),
  AI_HANDLE_PTR(AI_MAGIC_MARKER),
};

