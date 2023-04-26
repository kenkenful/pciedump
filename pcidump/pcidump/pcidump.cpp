#include <iostream>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <pciprop.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")



typedef struct _GuestPCIAddress {

	DWORD domain;
	DWORD bus;
	DWORD slot;
	DWORD function;
}GuestPCIAddress;


static void get_pci_address_for_device(GuestPCIAddress *pci, HDEVINFO dev_info);


int main()
{
	HDEVINFO dev_info = INVALID_HANDLE_VALUE;
	HDEVINFO parent_dev_info = INVALID_HANDLE_VALUE;

	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA dev_iface_data;
	HANDLE dev_file;

	int i;
	GuestPCIAddress *pci = (GuestPCIAddress*)malloc(sizeof(GuestPCIAddress));

	dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, 0, 0,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (dev_info == INVALID_HANDLE_VALUE) {
		std::cout << "failed to get devices tree    " << GetLastError() << std::endl;
		return -1;
	}

	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	dev_iface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for (i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		PSP_DEVICE_INTERFACE_DETAIL_DATA pdev_iface_detail_data = NULL;
		STORAGE_DEVICE_NUMBER sdn;
		char *parent_dev_id = NULL;
		SP_DEVINFO_DATA parent_dev_info_data;
		DWORD size = 0;
		//g_debug("getting device path");

		if (SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &GUID_DEVINTERFACE_DISK, 0, &dev_iface_data)) {


			if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_iface_data, pdev_iface_detail_data, size, &size, &dev_info_data)) {
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					pdev_iface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(size);
					pdev_iface_detail_data->cbSize = sizeof(*pdev_iface_detail_data);
				}
				else {
					std::cout << "failed to get device interfaces  " << GetLastError() << std::endl;
					//error_setg_win32(errp, GetLastError(),
					//	"failed to get device interfaces");
					//continue;
					
					goto end;
				}
			}
			if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_iface_data, pdev_iface_detail_data, size, &size, &dev_info_data)) {
				// pdev_iface_detail_data already is allocated
				
				std::cout << "failed to get device interfaces    " << GetLastError() << std::endl;
					//error_setg_win32(errp, GetLastError(),
					//"failed to get device interfaces");
				goto end;
				//continue;
			}

			dev_file = CreateFile(pdev_iface_detail_data->DevicePath, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

			if (!DeviceIoControl(dev_file, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &size, NULL)) {
				CloseHandle(dev_file);
				std::cout << "failed to get device slot number     " << GetLastError() << std::endl;
				//error_setg_win32(errp, GetLastError(),
				//	"failed to get device slot number");
				goto end;
				//continue;
			}
			CloseHandle(dev_file);

			//if (sdn.DeviceNumber != number) {
			std::cout << "PHYSICALDISK NUMBER: " << sdn.DeviceNumber << std::endl;
			//goto end;
			//if (sdn.DeviceNumber != number) {
			//	continue;
			//}
		}
		else {
			std::cout << "failed to get device interfaces      " << GetLastError() << std::endl;
			//error_setg_win32(errp, GetLastError(),
			//	"failed to get device interfaces");
			goto end;
			//continue;
		}


#if 1
		//g_debug("found device slot %d. Getting storage controller", number);


		{
			CONFIGRET cr;
			DEVINST dev_inst, parent_dev_inst;
			ULONG dev_id_size = 0;
			size = 0;
			if (!SetupDiGetDeviceInstanceId(dev_info, &dev_info_data, parent_dev_id, size, &size)) {
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					parent_dev_id = (char*)malloc(size);
				}
				else {
					std::cout << "failed to get device instance ID      " << GetLastError() << std::endl;

					//error_setg_win32(errp, GetLastError(),
						//"failed to get device instance ID");
					goto end;
				}
			}
			if (!SetupDiGetDeviceInstanceId(dev_info, &dev_info_data, parent_dev_id, size, &size)) {
				// parent_dev_id already is allocated
				std::cout << "failed to get device instance ID     " << GetLastError() << std::endl;
				//error_setg_win32(errp, GetLastError(),
				//	"failed to get device instance ID");
				goto end;
			}

			/*
			 * CM API used here as opposed to
			 * SetupDiGetDeviceProperty(..., DEVPKEY_Device_Parent, ...)
			 * which exports are only available in mingw-w64 6+
			 */
			cr = CM_Locate_DevInst(&dev_inst, parent_dev_id, 0);
			if (cr != CR_SUCCESS) {
				//g_error("CM_Locate_DevInst failed with code %lx", cr);
				//error_setg_win32(errp, GetLastError(),"failed to get device instance");
				std::cout << "failed to get device instance     " << GetLastError() << std::endl;
				goto end;
			}
			cr = CM_Get_Parent(&parent_dev_inst, dev_inst, 0);
			if (cr != CR_SUCCESS) {
				//g_error("CM_Get_Parent failed with code %lx", cr);
				//error_setg_win32(errp, GetLastError(),"failed to get parent device instance");
				std::cout << "failed to get parent device instance      " << GetLastError() << std::endl;
				goto end;
			}

			cr = CM_Get_Device_ID_Size(&dev_id_size, parent_dev_inst, 0);
			if (cr != CR_SUCCESS) {
				//g_error("CM_Get_Device_ID_Size failed with code %lx", cr);
				std::cout << "failed to get parent device ID length        " << GetLastError() << std::endl;
				//error_setg_win32(errp, GetLastError(),"failed to get parent device ID length");
				goto end;
			}

			++dev_id_size;
			if (dev_id_size > size) {
				free(parent_dev_id);
				parent_dev_id = (char*)malloc(dev_id_size);
			}

			cr = CM_Get_Device_ID(parent_dev_inst, parent_dev_id, dev_id_size, 0);

			if (cr != CR_SUCCESS) {
				//g_error("CM_Get_Device_ID failed with code %lx", cr);
				//error_setg_win32(errp, GetLastError(),"failed to get parent device ID");
				std::cout << "failed to get parent device ID         " << GetLastError() << std::endl;
				goto end;
			}
		}

		//g_debug("querying storage controller %s for PCI information",parent_dev_id);
		parent_dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_STORAGEPORT, parent_dev_id,NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (parent_dev_info == INVALID_HANDLE_VALUE) {
			//error_setg_win32(errp, GetLastError(),"failed to get parent device");
			std::cout << "failed to get parent device    " << GetLastError() << std::endl;
			goto end;
		}
		parent_dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

		if (!SetupDiEnumDeviceInfo(parent_dev_info, 0, &parent_dev_info_data)) {
			std::cout << "failed to get parent device data      " << GetLastError() << std::endl;
			//error_setg_win32(errp, GetLastError(),"failed to get parent device data");
			goto end;
		}
#endif

		//get_pci_address_for_device(pci, parent_dev_info);
		get_pci_address_for_device(pci, parent_dev_info);

		
		
		//break;
	}


end:
	if (parent_dev_info != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(parent_dev_info);
	}
	if (dev_info != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(dev_info);
	}
	
	return 0;


   
}







static void get_pci_address_for_device(GuestPCIAddress *pci, HDEVINFO dev_info)
{
	SP_DEVINFO_DATA dev_info_data;
	DWORD j;
	DWORD size;
	bool partial_pci = false;
	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

	for (j = 0; SetupDiEnumDeviceInfo(dev_info, j, &dev_info_data); j++) {
		DWORD addr=0, bus=0, ui_slot=0, type;
		int func=0, slot=0;
		size = sizeof(DWORD);
		/*
		* There is no need to allocate buffer in the next functions. The
		* size is known and ULONG according to
		* https://msdn.microsoft.com/en-us/library/windows/hardware/ff543095(v=vs.85).aspx
		*/
		if (!SetupDiGetDeviceRegistryProperty( dev_info, &dev_info_data, SPDRP_BUSNUMBER, &type, (PBYTE)&bus, size, NULL)) {
			//debug_error("failed to get PCI bus");
			std::cout << "failed to get PCI bus" << std::endl;
			bus = -1;
			partial_pci = true;
		}

		/*
		* The function retrieves the device's address. This value will be
		* transformed into device function and number
		*/
		if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data, SPDRP_ADDRESS, &type, (PBYTE)&addr, size, NULL)) {
			//debug_error("failed to get PCI address");
			std::cout << "failed to get PCI address" << std::endl;
			addr = -1;
			partial_pci = true;
		}

		/*
		* This call returns UINumber of DEVICE_CAPABILITIES structure.
		* This number is typically a user-perceived slot number.
		*/
		//if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data, SPDRP_UI_NUMBER, &type, (PBYTE)&ui_slot, size, NULL)) {
			//debug_error("failed to get PCI slot");
			//std::cout << "failed to get PCI slot" << std::endl;
			//ui_slot = -1;
			//partial_pci = true;
		//}

		/*
		* SetupApi gives us the same information as driver with
		* IoGetDeviceProperty. According to Microsoft:
		*
		*   FunctionNumber = (USHORT)((propertyAddress) & 0x0000FFFF)
		*   DeviceNumber = (USHORT)(((propertyAddress) >> 16) & 0x0000FFFF)
		*   SPDRP_ADDRESS is propertyAddress, so we do the same.
		*
		* https://docs.microsoft.com/en-us/windows/desktop/api/setupapi/nf-setupapi-setupdigetdeviceregistrypropertya
		*/
		//if (partial_pci) {
		//	pci->domain = -1;
		//	pci->slot = -1;
		//	pci->function = -1;
		//	pci->bus = -1;
		//	continue;
		//}
		//else {
			func = ((int)addr == -1) ? -1 : addr & 0x0000FFFF;
			slot = ((int)addr == -1) ? -1 : (addr >> 16) & 0x0000FFFF;
			//if ((int)ui_slot != slot) {
			//	//g_debug("mismatch with reported slot values: %d vs %d", (int)ui_slot, slot);
			//}
			pci->domain = 0;
			//pci->slot = (int)ui_slot;
			pci->function = func;
			pci->bus = (int)bus;
			std::cout << pci->domain << ":" << bus << ":" << slot << "." << func << std::endl;

			//return;
		//}

	}
}