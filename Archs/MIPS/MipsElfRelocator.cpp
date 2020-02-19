#include "stdafx.h"
#include "MipsElfRelocator.h"
#include "Parser/Parser.h"



MipsElfRelocator::MipsElfRelocator()
{
}

int MipsElfRelocator::expectedMachine() const
{
	return EM_MIPS;
}

bool MipsElfRelocator::relocateOpcode(int type, RelocationData& data, ByteArray& sectionData, int pos, Endianness endian)
{
	bool isOri;
	int64_t base;
	unsigned int p;
	int tempPos;

	unsigned int op = data.opcode;
	switch (type)
	{
	case R_MIPS_26: //j, jal
		op = (op & 0xFC000000) | (((op&0x03FFFFFF)+(data.relocationBase>>2))&0x03FFFFFF);
		break;
	case R_MIPS_32:
		op += (int) data.relocationBase;
		break;
	case R_MIPS_HI16:
		this->mipsHi = data;
		this->hiPos.push_front(pos);
		this->mipsHiIsMutated = false;
		this->mipsHiRelocOp = 0;
		return true;
	case R_MIPS_LO16:
		isOri = ((op >> 26) & 0x3F) == 0x0D;
		base = data.relocationBase;

		base += (isOri) ? op & 0xffff : (short)(op & 0xFFFF);

		if (pos < 0)
		{
			data.errorMessage = formatString(L"R_MIPS_LO16 missing R_MIPS_HI16 pair");
			return false;
		}
		//create R_MIPS_HI16 opcode
		base += (this->mipsHi.opcode & 0xFFFF) << 16;
		p = (this->mipsHi.opcode & 0xFFFF0000) | (((base >> 16) + (isOri ? 0 : (base & 0x8000) != 0)) & 0xFFFF);
		if (this->mipsHiIsMutated && this->mipsHiRelocOp != p)
		{
			tempPos = (this->hiLast.empty()) ? -1 : this->hiLast.front();

			Logger::printLine(L"Warning: R_MIPS_HI16 %X symbol address %X is being mutated by multiple R_MIPS_LO16 %X. Make sure object file is 0x10 byte aligned and not malformed", tempPos, base, pos);
		}
		if (!this->mipsHiIsMutated)
		{
			this->hiLast.clear();
			for (const int& position : this->hiPos)
			{
				sectionData.replaceDoubleWord(position, p, endian);
				this->hiLast.push_front(position);
			}
			this->hiPos.clear();
			this->mipsHiIsMutated = true;
			this->mipsHiRelocOp = p;
		}

		op = (op & 0xffff0000) | (base & 0xffff);
		break;
	default:
		data.errorMessage = formatString(L"Unknown MIPS relocation type %d",type);
		return false;
	}

	data.opcode = op;
	sectionData.replaceDoubleWord(pos, data.opcode, endian);
	return true;
}

void MipsElfRelocator::setSymbolAddress(RelocationData& data, int64_t symbolAddress, int symbolType)
{
	data.symbolAddress = symbolAddress;
	data.targetSymbolType = symbolType;
}

const wchar_t* mipsCtorTemplate = LR"(
	addiu	sp,-32
	sw		ra,0(sp)
	sw		s0,4(sp)
	sw		s1,8(sp)
	sw		s2,12(sp)
	sw		s3,16(sp)
	li		s0,%ctorTable%
	li		s1,%ctorTable%+%ctorTableSize%
	%outerLoopLabel%:
	lw		s2,(s0)
	lw		s3,4(s0)
	addiu	s0,8
	%innerLoopLabel%:
	lw		a0,(s2)
	jalr	a0
	addiu	s2,4h
	bne		s2,s3,%innerLoopLabel%
	nop
	bne		s0,s1,%outerLoopLabel%
	nop
	lw		ra,0(sp)
	lw		s0,4(sp)
	lw		s1,8(sp)
	lw		s2,12(sp)
	lw		s3,16(sp)
	jr		ra
	addiu	sp,32
	%ctorTable%:
	.word	%ctorContent%
)";

std::unique_ptr<CAssemblerCommand> MipsElfRelocator::generateCtorStub(std::vector<ElfRelocatorCtor>& ctors)
{
	Parser parser;
	if (ctors.size() != 0)
	{
		// create constructor table
		std::wstring table;
		for (size_t i = 0; i < ctors.size(); i++)
		{
			if (i != 0)
				table += ',';
			table += formatString(L"%s,%s+0x%08X",ctors[i].symbolName,ctors[i].symbolName,ctors[i].size);
		}

		return parser.parseTemplate(mipsCtorTemplate,{
			{ L"%ctorTable%",		Global.symbolTable.getUniqueLabelName() },
			{ L"%ctorTableSize%",	formatString(L"%d",ctors.size()*8) },
			{ L"%outerLoopLabel%",	Global.symbolTable.getUniqueLabelName() },
			{ L"%innerLoopLabel%",	Global.symbolTable.getUniqueLabelName() },
			{ L"%ctorContent%",		table },
		});
	} else {
		return parser.parseTemplate(L"jr ra :: nop");
	}
}
