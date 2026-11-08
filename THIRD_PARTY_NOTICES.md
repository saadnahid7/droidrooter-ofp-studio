# Third-party notices

OFP metadata decoding, key profiles, key derivation and Qualcomm section/hash
semantics are adapted from [Bjoern Kerler's oppo_decrypt](https://github.com/bkerler/oppo_decrypt),
revision `3456e850fb408f4dfab43e9788970c9be2caa743`.

Upstream source notices:

- `ofp_mtk_decrypt.py`: Oppo OFP MTK Decrypter (c) B. Kerler 2022, Licensed under MIT License.
- `ofp_qc_decrypt.py`: (c) B.Kerler 2018-2021, MIT license.

DroidRooter's OFP Studio ports these formats to native C and Windows system APIs. It does not
bundle or execute upstream Python code. The upstream README requests attribution;
please retain this notice with source and binary distributions.

The following MIT permission and disclaimer apply to adapted upstream material:

Copyright (c) 2018-2022 B. Kerler

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Windows cryptography and XML parsing are provided by Windows system DLLs and
are not redistributed by this project. MinGW-w64/GCC runtime support is linked
statically under its applicable runtime exceptions; no compiler runtime DLL is
distributed separately. Development-only PyCryptodome is not part of the EXE.
