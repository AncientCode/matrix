#include <windows.h>

static double HuetoRGB(double m1, double m2, double h )
{
	if( h < 0 ) h += 1.0;
	if( h > 1 ) h -= 1.0;

	if( 6.0*h < 1 )
		return (m1+(m2-m1)*h*6.0);
	
	if( 2.0*h < 1 )
		return m2;

	if( 3.0*h < 2.0 )
		return (m1+(m2-m1)*((2.0/3.0)-h)*6.0);

	return m1;
}

COLORREF HSLtoRGB( double H, double S, double L )
{
	double r,g,b;
	double m1, m2;
	
	if(S==0)
	{
		r=g=b=L;
	} 
	else
	{
		if(L <=0.5)
			m2 = L*(1.0+S);
		else
			m2 = L+S-L*S;
		m1 = 2.0*L-m2;

		r = HuetoRGB(m1,m2,H+1.0/3.0);
		g = HuetoRGB(m1,m2,H);
		b = HuetoRGB(m1,m2,H-1.0/3.0);
		
	} 
	
	return RGB((BYTE)(r*255),(BYTE)(g*255),(BYTE)(b*255));
}

COLORREF RGBtoHSL(COLORREF col, double *H, double *S, double *L)
{
	double r = GetRValue(col) / 255.0;
	double g = GetGValue(col) / 255.0;
	double b = GetBValue(col) / 255.0;

	double cMin = min(min(r, g), b);
	double cMax = max(max(r, g), b);
	double l = (cMax + cMin) / 2.0;
	double h, s;
	double delta = cMax-cMin;

	if(delta == 0)
	{
		s = 0;
		h  = 0;
	}
	else
	{
		if(l <= 0.5) 
			s = (cMax - cMin) / (cMax + cMin);
		else				    
			s = (cMax - cMin) / (2.0 - cMax - cMin);

		if(r == cMax)
			h = (g - b) / delta;
		else if(g == cMax)
			h = 2.0 + (b - r) / delta;
		else
			h = 4.0 + (r - g) / delta;

		h /= 6.0;
		if(h < 0)
			h += 1.0;

	}

	//*H = round(
	*H = h;
	*S = s;
	*L = l;
	return 0;
}


