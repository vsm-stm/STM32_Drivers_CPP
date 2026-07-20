#include "Modbus-slave.hpp"

SysInitStatus ModbusRTU_Slave::SetUp(){
	SysInitStatus status = USART::SetUp();

	if(status != SysInitStatus::InitOK
	|| dma_tx == nullptr
	|| dma_rx == nullptr){
		status = SysInitStatus::InitError;
		return status;
	}

	status = rs_DE.SetUp(PIN::TYPE::OUTPUT_PushPull);
	rs_DE = 0;
	USART::IRQ_en(USART::IRQ::TC, ENABLE);
	if(status != SysInitStatus::InitOK){
		return status;
	}

	AttachDMA(dma_tx, dma_rx);

	return status;
}; // SysInitStatus ModbusRTU_Slave::SetUp(){

void ModbusRTU_Slave::Rx_Process(){
	if(USART::GetRxStatus() == SysStatus::OK
	&& !USART::GetDataReceivedFlag()){
		USART::Receive_DMA(rx_buffer, sizeof(rx_buffer));
	}

	if(USART::GetDataReceivedFlag()){
		uint32_t data_len = USART::GetDataReceivedCount();

		if(Rx_Parse == nullptr
		|| data_len <= 3){
			USART::ClearDataReceivedFlag();
			return;
		}

		uint16_t rx_crc = CalculateCRC16(rx_buffer, data_len - 2);
		uint16_t data_crc = *(uint16_t*)&rx_buffer[data_len - 2];
		if(rx_crc == data_crc)
			Rx_Parse(rx_buffer, data_len-2);
	}
}; // void ModbusRTU_Slave::Rx_Process(){

uint8_t* ModbusRTU_Slave::GetTxBufferPtr(){
	if(tx_buffer_status != TxBuffStatus::Free){
		return nullptr;
	}

	tx_buffer_status = TxBuffStatus::OnFill;
	return tx_buffer;
}; //uint8_t* ModbusRTU_Slave::GetTxBufferPtr(){

SysStatus ModbusRTU_Slave::SendData(uint32_t len){
	if(USART::GetTxStatus() == SysStatus::Busy
	|| tx_buffer_status != TxBuffStatus::OnFill
	|| len == 0
	|| len > sizeof(tx_buffer) - 2){
		return SysStatus::Busy;
	}

	uint16_t tx_crc = CalculateCRC16(tx_buffer, len);
	*(uint16_t*)&tx_buffer[len] = tx_crc; // todo ask better solution for this

	tx_buffer_status = TxBuffStatus::OnSend;

	rs_DE = 1;
	SysStatus status = USART::Send_DMA(tx_buffer, len + 2);
	while(USART::GetTxStatus() == SysStatus::Busy){};

	return status;
}; // SysStatus ModbusRTU_Slave::SendData(uint8_t* data, uint32_t len){

void ModbusRTU_Slave::OnTC(){
	tx_buffer_status = TxBuffStatus::Free;
	rs_DE = 0;
} // void ModbusRTU_Slave::OnTC(){

uint16_t ModbusRTU_Slave::CalculateCRC16(uint8_t* data, uint32_t len)
{
	uint16_t crc = 0xFFFF;
	for (uint16_t i = 0; i < len; i++)
	{
		crc = (uint16_t)((crc << 8) ^ CRC16_table[(crc >> 8) ^ data[i]]);
	}
	return __builtin_bswap16(crc);
}