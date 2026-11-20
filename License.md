The vast majority of code in this repository was written by the Haiku Project,
and is licensed under the MIT license, which appears below. There are some
notable exceptions to this, such as the GNU C library (which is under the GPL)
as well as many other files in this repository which are under other licenses.
Every piece of source code should have its license specified at the top, but
there is also a (probably) comprehensive set of known licenses in the
`data/system/data/licenses` directory.


The MIT License
-----------------------------------------------------------

Copyright (c) <year> <copyright holders>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.


Appendix: code derived from VitruvianOS and Koan
-----------------------------------------------------------

The MIT notice above is reproduced unmodified. This appendix is a separate
notice for third-party code included in this tree.

Parts of this tree are derived from VitruvianOS
(https://github.com/VitruvianOS/Vitruvian) and from Koan
(https://github.com/carlos20260001/Koan), a VitruvianOS derivative.
VitruvianOS as a whole is distributed under the GNU General Public License,
version 2 (its `LICENSE`; the same text is in
`data/system/data/licenses/GNU GPL v2`). Each of its files keeps the license
stated in its own header, which is most often the MIT License (code that
comes from Haiku), and otherwise the GNU LGPL ("Distributed under the terms
of the LGPL License", `data/system/data/licenses/GNU LGPL v2.1`), the GNU
GPL, the NewOS License or the Be Sample Code License; the texts of all of
them are in `data/system/data/licenses`. The Nexus kernel modules in
`src/system/kernel/nexus` are GPL-2.0 (`SPDX-License-Identifier: GPL-2.0`),
Copyright (C) 2022-2026 Dario Casalinuovo; Koan's patches to Nexus are under
the same license. Code written for Koan that is not in VitruvianOS says
"Copyright 2026, Koan contributors" and is under the MIT License unless its
header says otherwise.

Each derived file keeps its original copyright header (for example
"Copyright 2019-2026, Dario Casalinuovo"), and each ported commit keeps its
original VitruvianOS author (git "Author") and records the source commit
("cherry picked from commit <sha>", or "Ported from VitruvianOS/Vitruvian
<sha>" for a snapshot). Modifications made here are additionally covered by
the license stated in each file.

A binary built from this tree that links or ships the GPL-2.0 parts is
distributed under the terms of the GPL-2.0 as a whole; the MIT-licensed
Haiku and air/OS code keeps its own notice.
