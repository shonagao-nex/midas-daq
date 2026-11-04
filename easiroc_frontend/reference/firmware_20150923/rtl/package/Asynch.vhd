--------------------------------------------------------------------------------
--! @file   Asynch.vhd
--! @brief  constans for asynch transfer
--! @author Takehiro Shiozaki
--! @date   2014-06-20
--------------------------------------------------------------------------------

package Asynch is

    constant C_FAST_CLK_FREQ        : integer := 500;
    constant C_TDC_CLK_FREQ         : integer := 125;
    constant C_SPI_CLK_FREQ         : integer := 66;
    constant C_EXT_CLK_FREQ         : integer := 50;
    constant C_SITCP_CLK_FREQ       : integer := 25;
    constant C_SLOWCONTROL_CLK_FREQ : integer := 6;
    constant C_ADC_CLK_FREQ         : integer := 6;
    constant C_AD9220_CLK_FREQ      : integer := 3;

    constant C_SITCP_CLK_TO_SLOWCONTROL_CLK  : integer := 2 * C_SITCP_CLK_FREQ / C_SLOWCONTROL_CLK_FREQ + 1;
    constant C_SITCP_CLK_TO_ADC_CLK          : integer := 2 * C_SITCP_CLK_FREQ / C_ADC_CLK_FREQ + 1;
    constant C_SITCP_CLK_TO_AD9220_CLK       : integer := 2 * C_SITCP_CLK_FREQ / C_AD9220_CLK_FREQ + 1;
    constant C_SLOWCONTROL_CLK_TO_AD9220_CLK : integer := 2 * C_SLOWCONTROL_CLK_FREQ / C_AD9220_CLK_FREQ + 1;
    constant C_ADC_CLK_TO_AD9200_CLK         : integer := 2 * C_ADC_CLK_FREQ / C_AD9220_CLK_FREQ + 1;
    constant C_FAST_CLK_TO_AD9220_CLK        : integer := 2 * C_FAST_CLK_FREQ / C_AD9220_CLK_FREQ + 1;
    constant C_FAST_CLK_TO_SITCP_CLK         : integer := 2 * C_FAST_CLK_FREQ / C_SITCP_CLK_FREQ + 1;

end Asynch;

package body Asynch is

end Asynch;
