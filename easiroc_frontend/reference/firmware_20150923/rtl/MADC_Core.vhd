--------------------------------------------------------------------------------
--! @file   MADC_Conre.vhd
--! @brief  Monitor ADC Conre
--! @author Naruhiro Chikuma
--! @date   2015-7-30
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;

entity MADC_Core is
    port(
               CLK       : in  std_logic;
               RST	 : in  std_logic;
               CH_SEL    : in  std_logic_vector(7 downto 0);
               ADC_RECV  : in  std_logic;
               ADC_SEND	 : out std_logic;
               CS_ADC	 : out std_logic;
               MADC_DATA : out std_logic_vector(15 downto 0);
               MADC_CLK	 : out std_logic
    );
end MADC_Core;

architecture RTL of MADC_Core is

	signal   adc_ch  : std_logic_vector(3 downto 0);  
	
	signal cnt     : std_logic_vector(31 downto 0); 
	signal w_reg   : std_logic_vector(23 downto 0); 
	signal clk_sw  : std_logic; 
	signal wr_en_r : std_logic; 
	signal data    : std_logic_vector(15 downto 0); 

begin
	MADC_DATA <= data;
	MADC_CLK <= CLK when clk_sw = '1' else '0';
	adc_ch <= CH_SEL(3 downto 0);

	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(RST = '1') then
			    cnt <= (others => '0');
			    CS_ADC           <= '1';
		    else
			    if(cnt > 200 and ADC_RECV = '1') then
				    cnt <= cnt + 1;
			    else
				    cnt <= cnt + 1;
				    CS_ADC <= '0';
			    end if;
		    end if;
	    end if;
	end process;

	process(CLK) begin
	    if(CLK'event and CLK = '1') then 

		    w_reg(23 downto 0) <= (w_reg(22 downto 0) & '1');
	    
		    -- ### Write ADC config reg (use only changing read channel now) 
		    if(CH_SEL <= 10) then 
			    if(cnt = 0) then
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    elsif(cnt = 10) then
				    clk_sw <= '1';
				    w_reg <= (X"10001" & adc_ch);
				    -- "0001000|00000000|0001" & adc_ch[3:0]
			    elsif(cnt = 40) then
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    end if;
		    	
		    -- ### read current config
		    elsif(CH_SEL = 254) then
			    if(cnt = 0) then
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    elsif(cnt = 3) then
				    clk_sw <= '1';
			    elsif(cnt = 5) then
				    w_reg(23 downto 16) <= "01010000";
			    elsif(cnt = 50) then -- 32'd50
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    end if;
			    
		    -- ### read ADC data
		    elsif(CH_SEL = 240) then
			    if(cnt = 0) then
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    elsif(cnt = 8) then
				    clk_sw <= '1';
				    w_reg(23 downto 16) <= "01011000";
			    elsif(cnt = 16) then
				    wr_en_r <='1';
			    elsif(cnt = 33) then
				    wr_en_r <='0';
			    elsif(cnt = 35) then
				    clk_sw <='0';
				    w_reg <= X"FFFFFF";
			    end if;
	
		    -- ### Write ADC mode reg(125Hz fix now)
		    elsif(CH_SEL = 248) then
			    if(cnt = 0) then
				    clk_sw <='0';
				    w_reg <= X"FFFFFF";
			    elsif(cnt = 10) then
				    clk_sw <= '1';
				    w_reg <= "000010000000000000000011"; -- X"080003"; 
			    elsif(cnt = 60) then
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    end if;
		    
		    -- ### ADC reset
		    elsif(CH_SEL = 255) then
			    if(cnt = 0) then
				    clk_sw <= '0';
				    w_reg <= X"FFFFFF";
			    elsif(cnt = 3) then   -- 32'd3
				    clk_sw <= '1';
			    elsif(cnt = 5) then   -- 32'd5
				    clk_sw <= '0';
			    end if;

		    -- ### Default ###
		    else
			    w_reg <= X"FFFFFF";
			    clk_sw <= '0';
		    end if;
	    end if;
	end process;

-- #### shift register Parallel to Serial ### ---
	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    ADC_SEND <= w_reg(23);
	    end if;
	end process;
	
-- ### shift register Serial to Parallel ### ---
	process(CLK) begin
	    if(CLK'event and CLK = '1') then 
		    if(wr_en_r = '1') then
			    data(15 downto 0) <= (data(14 downto 0) & ADC_RECV);
		    end if;
	    end if;
	end process;

end RTL;

