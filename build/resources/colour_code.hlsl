// Returns a colour blended between red and green based on x, 0=red, 0.5=yellow, 1=green
float3 ColourCodeRedGreen(float x)
{
    float3 colour;
    colour.r = saturate(2 - 2 * x);
    colour.g = saturate(2 * x);
    colour.b = 0;
    
    return colour;
}

float3 ColourCodeRedBlue(float x)
{
    float3 colour;
    colour.r = x > 1 ? saturate(4 * (x-1)) : saturate(2 - 4 * x);
    colour.g = saturate(2 - abs(2 - 4 * x));
    colour.b = saturate(2 * x - 1);
    
    return colour;
}

// Returns a colour blended between blue and red based on x, 0=blue, 0.25=cyan, 0.5=green, 0.75=yellow, 1=red, 1.25=purple
float3 ColourCodeBlueRed(float x)
{
    float3 colour;
    colour.r = saturate(4 * x - 2);
    colour.g = saturate(2 - abs(2 - 4 * x));
    colour.b = x > 1 ? saturate(4 * (x-1)) : saturate(2 - 4 * x);
    
    return colour;
}
