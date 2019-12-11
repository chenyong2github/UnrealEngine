#include "DMXProtocolInterfaceSACN.h"
#include "Interfaces/IDMXProtocol.h"

FDMXProtocolInterfaceSACN::FDMXProtocolInterfaceSACN(IDMXProtocol* InDMXProtocol)
	: DMXProtocol(InDMXProtocol)
{
}

IDMXProtocol* FDMXProtocolInterfaceSACN::GetProtocol() const
{
	return DMXProtocol;
}