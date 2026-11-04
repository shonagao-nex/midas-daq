--------------------------------------------------------------------------------
--! @file   TCP_Sender_32bit.vhd
--! @brief  read data from 32bit FIFO and send them to SiTCP
--! @author Takehiro Shiozaki
--! @date   2014-04-27
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity TCP_Sender_32bit is
    port ( CLK : in  std_logic;
           RESET : in  std_logic;
           DIN : in  std_logic_vector (31 downto 0);
           DOUT : out  std_logic_vector (7 downto 0);
           RE : out  std_logic;
           WE : out  std_logic;
           EMPTY : in  std_logic;
           AFULL : in  std_logic;
			  TCP_OPEN_ACK : in std_logic
			  );
end TCP_Sender_32bit;

architecture RTL of TCP_Sender_32bit is
	signal DinBuffer : std_logic_vector(31 downto 0);
	signal DinBufferEnable : std_logic;
	signal Ready : std_logic;
	type State is (IDLE, READ_FROM_FIFO, LOAD_TO_REG, WAIT_AFULL,
	               FIRST_BYTE, SECOND_BYTE, THIRD_BYTE, FOURTH_BYTE,
						THIRD_BYTE_WITH_READ_FROM_FIFO, FOURTH_BYTE_WITH_LOAD_TO_REG,
						FOURTH_BYTE_WITH_READ_FROM_FIFO);
	signal CurrentState : State;
	signal NextState : State;
	signal DoutSelect : std_logic_vector(1 downto 0);
	signal SitcpReady : std_logic;

begin

	SitcpReady <= (not AFULL) and TCP_OPEN_ACK;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			DinBuffer <= (others => '0');
		elsif(CLK'event and CLK = '1') then
			if(DinBufferEnable = '1') then
				DinBuffer <= Din;
			end if;
		end if;
	end process;

	process(CLK, RESET)
	begin
		if(RESET = '1') then
			CurrentState <= IDLE;
		elsif(CLK'event and CLK = '1') then
			CurrentState <= NextState;
		end if;
	end process;

	process(CurrentState, EMPTY, SitcpReady)
	begin
		case CurrentState is
			when IDLE =>
				if(EMPTY = '1') then
					NextState <= CurrentState;
				else
					NextState <= READ_FROM_FIFO;
				end if;
			when READ_FROM_FIFO =>
				NextState <= LOAD_TO_REG;
			when LOAD_TO_REG =>
				if(SitcpReady = '0') then
					NextState <= WAIT_AFULL;
				else
					NextState <= FIRST_BYTE;
				end if;
			when WAIT_AFULL =>
				if(SitcpReady = '0') then
					NextState <= CurrentState;
				else
					NextState <= FIRST_BYTE;
				end if;
			when FIRST_BYTE =>
				NextState <= SECOND_BYTE;
			when SECOND_BYTE =>
				if(EMPTY = '1') then
					NextState <= THIRD_BYTE;
				else
					NextState <= THIRD_BYTE_WITH_READ_FROM_FIFO;
				end if;
			when THIRD_BYTE =>
				if(EMPTY = '1') then
					NextState <= FOURTH_BYTE;
				else
					NextState <= FOURTH_BYTE_WITH_READ_FROM_FIFO;
				end if;
			when FOURTH_BYTE =>
				if(EMPTY = '1') then
					NextState <= IDLE;
				else
					NextState <= READ_FROM_FIFO;
				end if;
			when THIRD_BYTE_WITH_READ_FROM_FIFO =>
				NextState <= FOURTH_BYTE_WITH_LOAD_TO_REG;
			when FOURTH_BYTE_WITH_LOAD_TO_REG =>
				if(SitcpReady = '0') then
					NextState <= WAIT_AFULL;
				else
					NextState <= FIRST_BYTE;
				end if;
			when FOURTH_BYTE_WITH_READ_FROM_FIFO =>
				NextState <= LOAD_TO_REG;
		end case;
	end process;

	DoutSelect <= "00" when CurrentState = FIRST_BYTE else
	              "01" when CurrentState = SECOND_BYTE else
					  "10" when CurrentState = THIRD_BYTE or
					            CurrentState = THIRD_BYTE_WITH_READ_FROM_FIFO else
					  "11";

	with(DoutSelect) select
	DOUT <= DinBuffer(31 downto 24) when "00",
	        DinBuffer(23 downto 16) when "01",
			  DinBuffer(15 downto  8) when "10",
			  DinBuffer( 7 downto  0) when others;

	DinBufferEnable <= '1' when CurrentState = LOAD_TO_REG or
	                            CurrentState = FOURTH_BYTE_WITH_LOAD_TO_REG else
	                   '0';

	RE <= '1' when CurrentState = READ_FROM_FIFO or
	               CurrentState = THIRD_BYTE_WITH_READ_FROM_FIFO or
						CurrentState = FOURTH_BYTE_WITH_READ_FROM_FIFO else
			'0';

	WE <= '1' when CurrentState = FIRST_BYTE or
	               CurrentState = SECOND_BYTE or
						CurrentState = THIRD_BYTE or
						CurrentState = FOURTH_BYTE or
						CurrentState = THIRD_BYTE_WITH_READ_FROM_FIFO or
						CurrentState = FOURTH_BYTE_WITH_LOAD_TO_REG or
						CurrentState = FOURTH_BYTE_WITH_READ_FROM_FIFO else
			'0';
end RTL;
