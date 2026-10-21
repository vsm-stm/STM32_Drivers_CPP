#include "Modbus-slave.hpp"

SysInitStatus ModbusRTU_Slave::SetUp(FrameFormat format){
	SysInitStatus status = USART::SetUp(USART::FIFO::NO, USART::FIFO_TH::NO,
		USART::FIFO_TH::NO, format);

	if (status != SysInitStatus::InitOK) return status;

	// SetUp preloads LOW before switching GPIO to output.
	rs_DE.SetUp(PIN::TYPE::OUTPUT_PushPull);

	return status;
}; // SysInitStatus ModbusRTU_Slave::SetUp(){

void ModbusRTU_Slave::DeInit()
{
	if (USART::GetTxStatus() == SysStatus::NotInit) return;
	USART::DeInit();
	rs_DE = 0;
	tx_buffer_status = TxBuffStatus::Free;
}

void ModbusRTU_Slave::Rx_Process(){
	const SysStatus rx_status = USART::GetRxStatus();
	if((rx_status == SysStatus::OK || rx_status == SysStatus::Error)
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
		const uint16_t data_crc = static_cast<uint16_t>(rx_buffer[data_len - 2])
			| (static_cast<uint16_t>(rx_buffer[data_len - 1]) << 8);
		if(rx_crc == data_crc)
			Rx_Parse(rx_buffer, data_len-2);
		USART::ClearDataReceivedFlag();
	}
}; // void ModbusRTU_Slave::Rx_Process(){

uint8_t* ModbusRTU_Slave::GetTxBufferPtr(){
	if(USART::GetTxStatus() == SysStatus::NotInit || tx_buffer_status != TxBuffStatus::Free){
		return nullptr;
	}

	tx_buffer_status = TxBuffStatus::OnFill;
	return tx_buffer;
}; //uint8_t* ModbusRTU_Slave::GetTxBufferPtr(){

SysStatus ModbusRTU_Slave::CancelTxBuffer()
{
	if (USART::GetTxStatus() == SysStatus::NotInit) return SysStatus::NotInit;
	if (tx_buffer_status == TxBuffStatus::OnSend) return SysStatus::Busy;
	tx_buffer_status = TxBuffStatus::Free;
	return SysStatus::OK;
}

SysStatus ModbusRTU_Slave::SendData(uint32_t len){
	const SysStatus uart_status = USART::GetTxStatus();
	if (uart_status == SysStatus::NotInit) return SysStatus::NotInit;
	if (uart_status == SysStatus::Busy || tx_buffer_status == TxBuffStatus::OnSend)
		return SysStatus::Busy;
	if (tx_buffer_status != TxBuffStatus::OnFill || len == 0 || len > sizeof(tx_buffer) - 2)
		return SysStatus::Error;

	uint16_t tx_crc = CalculateCRC16(tx_buffer, len);
	tx_buffer[len] = static_cast<uint8_t>(tx_crc);
	tx_buffer[len + 1] = static_cast<uint8_t>(tx_crc >> 8);

	tx_buffer_status = TxBuffStatus::OnSend;

	rs_DE = 1;
	SysStatus status = USART::Send_DMA(tx_buffer, len + 2);
	if (status != SysStatus::OK) {
		rs_DE = 0;
		tx_buffer_status = TxBuffStatus::OnFill; // Keep the prepared response for retry.
	} else if (tx_buffer_status == TxBuffStatus::OnSend) {
		// Send_DMA cleared stale TC before starting. Enable the wire-complete
		// notification now; a very short response may already have reached TC.
		USART::IRQ_en(USART::IRQ::TC, ENABLE);
	}

	return status;
}; // SysStatus ModbusRTU_Slave::SendData(uint8_t* data, uint32_t len){

void ModbusRTU_Slave::OnTC(){
	USART::IRQ_en(USART::IRQ::TC, DISABLE); // Source only; registration remains.
	if (tx_buffer_status != TxBuffStatus::OnSend) return;
	rs_DE = 0;
	tx_buffer_status = TxBuffStatus::Free;
} // void ModbusRTU_Slave::OnTC(){

void ModbusRTU_Slave::OnError(uint32_t errors)
{
	// RX errors must not release DE during an independent response transmission.
	if (errors & static_cast<uint32_t>(USART::Error::DmaTx)) {
		USART::IRQ_en(USART::IRQ::TC, DISABLE);
		rs_DE = 0;
		tx_buffer_status = TxBuffStatus::Free;
	}
}

uint16_t ModbusRTU_Slave::CalculateCRC16(uint8_t* data, uint32_t len)
{
	uint16_t crc = 0xFFFF;
	for (uint16_t i = 0; i < len; i++)
	{
		crc = (uint16_t)((crc << 8) ^ CRC16_table[(crc >> 8) ^ data[i]]);
	}
	return __builtin_bswap16(crc);
}
