#include <iostream>
#include <windows.h>
#include <initguid.h>
#include <devpropdef.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#define INITGUID

#include <devpkey.h>
#include <pciprop.h>
#include <memory>
#include <vector>
#include <iomanip>

#pragma comment (lib, "setupapi.lib")
#pragma comment (lib, "cfgmgr32.lib")

typedef struct _PICE_INFO {
	WORD domain;
	WORD bus;
	WORD dev;
	WORD func;
	WORD secondary_domain;
	WORD secondary_bus;
	WORD secondary_dev;

	WORD secondary_func;
	DWORD physicaldrive_no;
	char dev_inst_path[MAX_PATH] = { 0 };
	char disk_inst_path[MAX_PATH] = { 0 };

	UINT32 classCode;
	UINT32 subClassCode;
	UINT32 progIF;

}PCIE_INFO;

std::vector<std::unique_ptr<PCIE_INFO>> list_nvme(void);
std::vector<std::unique_ptr<PCIE_INFO>> list_pci(BOOL);


bool GetUInt32Property(DEVINST devInst, const DEVPROPKEY& propKey, UINT32& result) {
	DEVPROPTYPE propertyType;
	ULONG bufferSize = sizeof result;

	auto cr = CM_Get_DevNode_PropertyW(
		devInst,
		&propKey,
		&propertyType,
		reinterpret_cast<BYTE*>(&result),
		&bufferSize, 0
	);
	return cr == CR_SUCCESS && propertyType == DEVPROP_TYPE_UINT32;
}


std::vector<std::unique_ptr<PCIE_INFO>> list_nvme() {
	return list_pci(true);
}

std::vector<std::unique_ptr<PCIE_INFO>> list_pci(BOOL isNvme) {
	std::vector<std::unique_ptr<PCIE_INFO>> pcie_info;

	SP_DEVINFO_DATA pci_info_data = { 0 };
	pci_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

	DWORD domain = 0, bus = 0, dev = 0, func = 0, bus_num = 0;
	ULONG reg_type = 0, reg_len = 0;
	DWORD address = 0;

	HDEVINFO hdi = SetupDiGetClassDevs(nullptr, "PCI", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (hdi == INVALID_HANDLE_VALUE) {
		std::cerr << "failed to get device tree, errno: " << GetLastError() << std::endl;
		exit;
	}

	for (int i = 0; SetupDiEnumDeviceInfo(hdi, i, &pci_info_data); ++i) {
		UINT32 classCode;
		UINT32 subClassCode;
		UINT32 progIF;

		if (!GetUInt32Property(pci_info_data.DevInst, DEVPKEY_PciDevice_BaseClass, classCode)
			|| !GetUInt32Property(pci_info_data.DevInst, DEVPKEY_PciDevice_SubClass, subClassCode)
			|| !GetUInt32Property(pci_info_data.DevInst, DEVPKEY_PciDevice_ProgIf, progIF)) {
			continue;
		}

		if (isNvme) {
			if (classCode == 0x1 && subClassCode == 0x8 && progIF == 0x2) {
				// NVMe device		
			}
			else {
				// not NVMe device
				continue;
			}
		}

		std::unique_ptr<PCIE_INFO> pcie_if = std::make_unique<PCIE_INFO>();

		pcie_if->classCode = classCode;
		pcie_if->subClassCode = subClassCode;
		pcie_if->progIF = progIF;
		pcie_if->physicaldrive_no = 0xffffffff;

		reg_len = sizeof(bus_num);

		auto cr = CM_Get_DevNode_Registry_PropertyA(pci_info_data.DevInst, CM_DRP_BUSNUMBER, &reg_type, &bus_num, & reg_len, 0 );
		if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(bus_num)) {
			pcie_if->domain = bus_num >> 8;
			pcie_if->bus = bus_num & 0xff;
		}
		else {
			continue;
		}

		cr = CM_Get_DevNode_Registry_PropertyA(pci_info_data.DevInst, CM_DRP_ADDRESS, &reg_type, &address, &reg_len, 0);
		if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(address)) {
			pcie_if->dev = address >> 16;
			pcie_if->func = address & 0xffff;
		}
		else {
			continue;
		
		}

		char* device_instance_path = nullptr;
		DWORD size = 0;

		cr = CM_Get_Device_ID_Size(&size, pci_info_data.DevInst, 0);

		if (cr != CR_SUCCESS) {
			std::cerr << "failed to get device instance id : " << GetLastError() << std::endl;
			continue;
		}

		++size;
		device_instance_path = (char*)malloc(size);

		cr = CM_Get_Device_ID(pci_info_data.DevInst, device_instance_path, size, 0);

		strcpy_s(pcie_if->dev_inst_path, MAX_PATH, device_instance_path);

		DEVINST parent_dev_inst = 0;

		cr = CM_Get_Parent(&parent_dev_inst, pci_info_data.DevInst, 0);
		if (cr == CR_SUCCESS) {
			CM_Get_DevNode_Registry_PropertyA(parent_dev_inst, CM_DRP_BUSNUMBER, &reg_type, &bus_num, &reg_len, 0);
			if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(bus_num)) {
				pcie_if->secondary_domain = bus_num >> 8;
				pcie_if->secondary_domain = bus_num & 0xff;
			}
			else {
				continue;
			}
		
			CM_Get_DevNode_Registry_PropertyA(parent_dev_inst, CM_DRP_ADDRESS, &reg_type, &address, &reg_len, 0);
			if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(address)) {
				pcie_if->secondary_dev = address >> 16;
				pcie_if->secondary_func = address & 0xffff;
			}
			else {
				continue;
			}
		}
		else {
			std::cerr << "secondary bus is not found :" << GetLastError() << std::endl;
			continue;
		}

		pcie_info.push_back(std::move(pcie_if));

	}

	STORAGE_DEVICE_NUMBER sdn = { 0 };
	SP_DEVICE_INTERFACE_DATA disk_info_data = { 0 };

	HDEVINFO dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, 0,0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		std::cerr << "failed to get device tree: " << GetLastError() << std::endl;
		exit;
	}

	disk_info_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for (int i = 0; SetupDiEnumDeviceInfo(dev_info, i, &pci_info_data); ++i) {
		PSP_DEVICE_INTERFACE_DETAIL_DATA pdev_iface_detail_data = nullptr;
		char* device_instance_path = nullptr;

		SP_DEVINFO_DATA dev_info_data;

		DWORD size = 0;
		char* disk_device_instance_path = nullptr;

		{
			DWORD size = 0;
			auto cr = CM_Get_Device_ID_Size(&size, pci_info_data.DevInst, 0);
			if (cr != CR_SUCCESS) {
				std::cerr << "failed to get disk device instance id: " << GetLastError() << std::endl;
				continue;
			}
			++size;
			disk_device_instance_path = (char*)malloc(size);
			cr = CM_Get_Device_ID(pci_info_data.DevInst, disk_device_instance_path, size, 0);
		}

		if (SetupDiEnumDeviceInterfaces(dev_info, &pci_info_data, &GUID_DEVINTERFACE_DISK, 0, &disk_info_data)) {
			if (!SetupDiGetDeviceInterfaceDetail(dev_info, &disk_info_data, pdev_iface_detail_data, size, &size, &pci_info_data)) {
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					pdev_iface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(size);
					pdev_iface_detail_data->cbSize = sizeof(*pdev_iface_detail_data);
				}
				else {
					std::cerr << "failed to get device interface: " << GetLastError() << std::endl;
					continue;
				}
			}

			if (!SetupDiGetDeviceInterfaceDetail(dev_info, &disk_info_data, pdev_iface_detail_data, size, &size, &pci_info_data)) {
				std::cerr << "failed to get device interfaces: " << GetLastError() << std::endl;
				continue;
			}

			HANDLE dev_file = CreateFile(pdev_iface_detail_data->DevicePath, 0, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

			if (dev_file == INVALID_HANDLE_VALUE) {
				std::cerr << "failed to CreateFile: " << GetLastError() << std::endl;
				continue;
			}

			if (!DeviceIoControl(dev_file, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &size, nullptr)) {
				CloseHandle(dev_file);
				std::cerr << "failed to get device slot number: " << GetLastError() << std::endl;
				continue;
			}

			CloseHandle(dev_file);
		}
		else {
			std::cerr << "failed to get device interface: " << GetLastError() << std::endl;
			continue;
		}

		char* bus_relation = nullptr;

		// Get Bus relation Size and create buffer
		if (!SetupDiGetDeviceInstanceId(dev_info, &pci_info_data, bus_relation, size, &size)) {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				bus_relation = (char*)malloc(size);
			}
			else {
				std::cerr << "failed to get device instance ID: " << GetLastError() << std::endl;
				continue;
			}
		}

		// Get Bus relation
		if (!SetupDiGetDeviceInstanceId(dev_info, &pci_info_data, bus_relation, size, &size)) {
			std::cerr << "failed to get device instance ID: " << GetLastError() << std::endl;
			continue;
		}

		DEVINST dev_inst, parent_dev_inst;
		size = 0;

		auto cr = CM_Locate_DevInst(&dev_inst, bus_relation, 0);
		if (cr != CR_SUCCESS) {
			std::cerr << "failed to get device instance: " << GetLastError() << std::endl;
			goto end;
		}

		cr = CM_Get_Parent(&parent_dev_inst, dev_inst, 0);
		if (cr != CR_SUCCESS) {
			std::cerr << "failed to get parent device instance : " << GetLastError() << std::endl;
		}

		cr = CM_Get_Device_ID_Size(&size, parent_dev_inst, 0);
		if (cr != CR_SUCCESS) {
			std::cerr << "failed to get parenet device ID length:  " << GetLastError() << std::endl;
			continue;
		}

		++size;
		device_instance_path = (char*)malloc(size);
		
		cr = CM_Get_Device_ID(parent_dev_inst, device_instance_path, size, 0);
		if (cr != CR_SUCCESS) {
			std::cerr << "failed to get parenet device ID: " << GetLastError << std::endl;
			continue;
		}

		HDEVINFO parent_dev_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_STORAGEPORT, device_instance_path, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (parent_dev_info == INVALID_HANDLE_VALUE) {
			std::cerr << "failed to get device instance path: " << GetLastError() << std::endl;
			continue;
		}

		dev_info_data.cbSize = sizeof( SP_DEVINFO_DATA);

		if (!SetupDiEnumDeviceInfo(parent_dev_info, 0, &dev_info_data)) {
			std::cerr << "failed to get parent device data: " << GetLastError() << std::endl;
			continue;
		}

		cr = CM_Get_DevNode_Registry_PropertyA(dev_info_data.DevInst, CM_DRP_BUSNUMBER, &reg_type, &bus_num, &reg_len, 0);
		if(cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(bus_num)){
			domain = bus_num >> 8;
			bus = bus_num & 0xff;
		}
		else {
			std::cerr << "failed to get domain and bus " << std::endl;
			continue;
		}

		cr = CM_Get_DevNode_Registry_PropertyA(dev_info_data.DevInst, CM_DRP_ADDRESS, &reg_type, &address, &reg_len, 0);
		if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(address)) {
			dev = address >> 16;
			func = address & 0xffff;
		}
		else {
			std::cerr << "failed to get dev and func " << std::endl;
			continue;
		}

		//std::cout << std::hex;
		//std::cout <<  std::setw(4) << std::setfill('0') << domain << std::setw(2) << bus << ":" << std::setw(2) << dev << "." << std::setw(2) << func << std::endl;

		for (auto& v : pcie_info) {
			if (v->domain == domain && v->bus == bus && v->dev == dev && v->func == func) {
				v->physicaldrive_no = sdn.DeviceNumber;
				strcpy_s(v->disk_inst_path, MAX_PATH, disk_device_instance_path);
				break;
			}
		}

		if (parent_dev_info) SetupDiDestroyDeviceInfoList(parent_dev_info);

	}

end:
	if (hdi) SetupDiDestroyDeviceInfoList(hdi);
	if (dev_info) SetupDiDestroyDeviceInfoList(dev_info);

	return pcie_info;
}

void print(std::vector<std::unique_ptr<PCIE_INFO>> &vec) {
	for (auto& v : vec) {
		std::cout << "-------" << std::endl;
		std::cout << std::hex;
		std::cout << "Physical Disk No: " << v->physicaldrive_no << std::endl;
		std::cout << "Class Code: " << v->classCode << ", Sub Class Code: " << v->subClassCode << std::endl;
		std::cout << "Primary BDF: " << std::setw(4) << std::setfill('0') << v-> domain << std::setw(2) << v-> bus << ":" << std::setw(2) << v -> dev << "." << std::setw(2) << v-> func << std::endl;
		std::cout << "Secondary BDF: " << std::setw(4) << std::setfill('0') << v->secondary_domain << std::setw(2) << v->secondary_bus << ":" << std::setw(2) << v->secondary_dev << "." << std::setw(2) << v->secondary_func << std::endl;
		std::cout << "Device Instance path: " << v->dev_inst_path << std::endl;
		std::cout << "Disk Instance path: " << v->disk_inst_path << std::endl;
	}
}

int main() {
	auto u = list_nvme();
	print(u);
	system("pause");
}
