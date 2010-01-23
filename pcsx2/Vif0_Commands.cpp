/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2009  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "Common.h"
#include "Vif_Dma.h"
#include "VUmicro.h"

//------------------------------------------------------------------
// Vif0 Data Transfer Commands
//------------------------------------------------------------------

static int __fastcall Vif0TransNull(u32 *data)  // Shouldnt go here
{
	Console.WriteLn("VIF0 Shouldn't go here CMD = %x", vif0Regs->code);
	vif0.cmd = 0;
	return 0;
}

static int __fastcall Vif0TransSTMask(u32 *data)  // STMASK
{
	vif0Regs->mask = data[0];
	VIF_LOG("STMASK == %x", vif0Regs->mask);

	vif0.tag.size = 0;
	vif0.cmd = 0;
	return 1;
}

static int __fastcall Vif0TransSTRow(u32 *data)  // STROW
{
	int ret;

	u32* pmem = &vif0Regs->r0 + (vif0.tag.addr << 2);
	u32* pmem2 = g_vifmask.Row0 + vif0.tag.addr;
	pxAssume(vif0.tag.addr < 4);
	ret = min(4 - vif0.tag.addr, vif0.vifpacketsize);
	pxAssume(ret > 0);

	switch (ret)
	{
		case 4:
			pmem[12] = data[3];
			pmem2[3] = data[3];
		case 3:
			pmem[8] = data[2];
			pmem2[2] = data[2];
		case 2:
			pmem[4] = data[1];
			pmem2[1] = data[1];
		case 1:
			pmem[0] = data[0];
			pmem2[0] = data[0];
			break;

			jNO_DEFAULT
	}

	vif0.tag.addr += ret;
	vif0.tag.size -= ret;
	if (vif0.tag.size == 0) vif0.cmd = 0;

	return ret;
}

static int __fastcall Vif0TransSTCol(u32 *data)  // STCOL
{
	int ret;

	u32* pmem = &vif0Regs->c0 + (vif0.tag.addr << 2);
	u32* pmem2 = g_vifmask.Col0 + vif0.tag.addr;
	ret = min(4 - vif0.tag.addr, vif0.vifpacketsize);

	switch (ret)
	{
		case 4:
			pmem[12] = data[3];
			pmem2[3] = data[3];
		case 3:
			pmem[8] = data[2];
			pmem2[2] = data[2];
		case 2:
			pmem[4] = data[1];
			pmem2[1] = data[1];
		case 1:
			pmem[0] = data[0];
			pmem2[0] = data[0];
			break;

			jNO_DEFAULT
	}

	vif0.tag.addr += ret;
	vif0.tag.size -= ret;
	if (vif0.tag.size == 0) vif0.cmd = 0;
	return ret;
}

static __forceinline void vif0mpgTransfer(u32 addr, u32 *data, int size)
{
	if (memcmp(VU0.Micro + addr, data, size << 2))
	{
		CpuVU0->Clear(addr, size << 2); // Clear before writing! :/ (cottonvibes)
		memcpy_fast(VU0.Micro + addr, data, size << 2);
	}
}

static int __fastcall Vif0TransMPG(u32 *data)  // MPG
{
	if (vif0.vifpacketsize < vif0.tag.size)
	{
		if((vif0.tag.addr + vif0.vifpacketsize) > 0x1000) DevCon.Warning("Vif0 MPG Split Overflow");

		vif0mpgTransfer(vif0.tag.addr, data, vif0.vifpacketsize);
		vif0.tag.addr += vif0.vifpacketsize << 2;
		vif0.tag.size -= vif0.vifpacketsize;

		return vif0.vifpacketsize;
	}
	else
	{
		int ret;

		if((vif0.tag.addr + vif0.tag.size) > 0x1000) DevCon.Warning("Vif0 MPG Overflow");

		vif0mpgTransfer(vif0.tag.addr, data, vif0.tag.size);
		ret = vif0.tag.size;
		vif0.tag.size = 0;
		vif0.cmd = 0;

		return ret;
	}
}

static int __fastcall Vif0TransUnpack(u32 *data)	// UNPACK
{
	return nVifUnpack(0, (u8*)data);
}

//------------------------------------------------------------------
// Vif0 CMD Base Commands
//------------------------------------------------------------------

static void Vif0CMDNop()  // NOP
{
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDSTCycl()  // STCYCL
{
	vif0Regs->cycle.cl = (u8)vif0Regs->code;
	vif0Regs->cycle.wl = (u8)(vif0Regs->code >> 8);
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDITop()  // ITOP
{
	vif0Regs->itops = vif0Regs->code & 0x3ff;
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDSTMod()  // STMOD
{
	vif0Regs->mode = vif0Regs->code & 0x3;
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDMark()  // MARK
{
	vif0Regs->mark = (u16)vif0Regs->code;
	vif0Regs->stat.MRK = true;
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDFlushE()  // FLUSHE
{
	vif0FLUSH();
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDMSCALF()  //MSCAL/F
{
	vuExecMicro<0>((u16)(vif0Regs->code) << 3);
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDMSCNT()  // MSCNT
{
	vuExecMicro<0>(-1);
	vif0.cmd &= ~0x7f;
}

static void Vif0CMDSTMask()  // STMASK
{
	vif0.tag.size = 1;
}

static void Vif0CMDSTRowCol() // STROW / STCOL
{
	vif0.tag.addr = 0;
	vif0.tag.size = 4;
}

static void Vif0CMDMPGTransfer()  // MPG
{
	int vifNum;
	vif0FLUSH();
	vifNum = (u8)(vif0Regs->code >> 16);
	if (vifNum == 0) vifNum = 256;
	vif0.tag.addr = (u16)((vif0Regs->code) << 3) & 0xfff;
	vif0.tag.size = vifNum * 2;
}

static void Vif0CMDNull()  // invalid opcode
{
	// if ME1, then force the vif to interrupt
	if (!(vif0Regs->err.ME1))    //Ignore vifcode and tag mismatch error
	{
		Console.WriteLn("UNKNOWN VifCmd: %x", vif0.cmd);
		vif0Regs->stat.ER1 = true;
		vif0.irq++;
	}
	vif0.cmd &= ~0x7f;
}


//------------------------------------------------------------------
// Vif0 Data Transfer / Vif0 CMD Tables
//------------------------------------------------------------------
 
int (__fastcall *Vif0TransTLB[128])(u32 *data) =
{
	Vif0TransNull	 , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0x7*/
	Vif0TransNull	 , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0xF*/
	Vif0TransNull	 , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull	  , Vif0TransNull   , /*0x17*/
	Vif0TransNull    , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0x1F*/
	Vif0TransSTMask  , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull	  , Vif0TransNull   , /*0x27*/
	Vif0TransNull    , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull	  , Vif0TransNull   , /*0x2F*/
	Vif0TransSTRow	 , Vif0TransSTCol	, Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull	  , Vif0TransNull   , /*0x37*/
	Vif0TransNull    , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0x3F*/
	Vif0TransNull    , Vif0TransNull    , Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0x47*/
	Vif0TransNull    , Vif0TransNull    , Vif0TransMPG	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0x4F*/
	Vif0TransNull	 , Vif0TransNull	, Vif0TransNull	  , Vif0TransNull   , Vif0TransNull	  , Vif0TransNull	, Vif0TransNull	  , Vif0TransNull   , /*0x57*/
	Vif0TransNull	 , Vif0TransNull	, Vif0TransNull	  , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , Vif0TransNull   , /*0x5F*/
	Vif0TransUnpack  , Vif0TransUnpack  , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransNull   , /*0x67*/
	Vif0TransUnpack  , Vif0TransUnpack  , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , /*0x6F*/
	Vif0TransUnpack  , Vif0TransUnpack  , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransNull   , /*0x77*/
	Vif0TransUnpack  , Vif0TransUnpack  , Vif0TransUnpack , Vif0TransNull   , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack , Vif0TransUnpack   /*0x7F*/
};

void (*Vif0CMDTLB[75])() =
{
	Vif0CMDNop	   , Vif0CMDSTCycl  , Vif0CMDNull		, Vif0CMDNull , Vif0CMDITop  , Vif0CMDSTMod , Vif0CMDNull, Vif0CMDMark , /*0x7*/
	Vif0CMDNull	   , Vif0CMDNull    , Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull    , Vif0CMDNull , /*0xF*/
	Vif0CMDFlushE   , Vif0CMDNull   , Vif0CMDNull		, Vif0CMDNull, Vif0CMDMSCALF, Vif0CMDMSCALF, Vif0CMDNull	, Vif0CMDMSCNT, /*0x17*/
	Vif0CMDNull    , Vif0CMDNull    , Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull    , Vif0CMDNull , /*0x1F*/
	Vif0CMDSTMask  , Vif0CMDNull    , Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull	, Vif0CMDNull , /*0x27*/
	Vif0CMDNull    , Vif0CMDNull    , Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull	, Vif0CMDNull , /*0x2F*/
	Vif0CMDSTRowCol, Vif0CMDSTRowCol, Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull	, Vif0CMDNull , /*0x37*/
	Vif0CMDNull    , Vif0CMDNull    , Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull    , Vif0CMDNull , /*0x3F*/
	Vif0CMDNull    , Vif0CMDNull    , Vif0CMDNull		, Vif0CMDNull , Vif0CMDNull  , Vif0CMDNull  , Vif0CMDNull    , Vif0CMDNull , /*0x47*/
	Vif0CMDNull    , Vif0CMDNull    , Vif0CMDMPGTransfer
};