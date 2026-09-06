%==========================================================================
% TestUartSending.m - Verify UART reception from STM32 Master Board
%==========================================================================
% Hardware: STM32F103C8T6 Master  |  USART1 PA9(TX)/PA10(RX) @ 115200 8N1
% Wiring: USB-TTL RX -> PA9 (Master TX), GND common
% Note: Master only transmits when SW_UART (PA15) is ON (active-low).
%==========================================================================
clc;
clearvars;

COM_PORT = "COM5";
BAUD_RATE = 115200;

fprintf('====================================================\n');
fprintf('  BMS UART Reception Test - %s @ %d baud\n', COM_PORT, BAUD_RATE);
fprintf('====================================================\n');

% Release orphaned MATLAB serial handles from previous crashed sessions
try
	orphan = serialportfind;
	if ~isempty(orphan)
		delete(orphan);
		fprintf('Released %d orphaned serial handle(s).\n', numel(orphan));
	end
catch
end

% Ensure port is released on exit / Ctrl+C
cleanupObj = onCleanup(@() cleanup_test_port(COM_PORT));

% Open serial port
try
	s = serialport(COM_PORT, BAUD_RATE);
catch ME
	fprintf('Failed to open %s: %s\n', COM_PORT, ME.message);
	fprintf('Check: Device Manager COM number, close terminals holding the port, then retry.\n');
	return;
end

s.Timeout = 2;
configureTerminator(s, "LF");
flush(s);

fprintf('Listening on %s @ %d baud. Waiting for UART data...\n', COM_PORT, BAUD_RATE);
fprintf('Master SW_UART (PA15) must be ON, otherwise no data is sent.\n');
fprintf('Press Ctrl+C to stop.\n');
fprintf('----------------------------------------------------\n');

while true
	try
		line = readline(s);
	catch ME
		fprintf('Read error: %s\n', ME.message);
		pause(0.2);
		continue;
	end

	if strlength(strtrim(line)) == 0
		continue;
	end

	fprintf('UART Data: %s\n', strtrim(line));
end

function cleanup_test_port(portName)
	try
		ports = serialportfind("Port", portName);
		if ~isempty(ports)
			delete(ports);
			fprintf('\nSerial port %s closed.\n', portName);
		end
	catch
	end
end
