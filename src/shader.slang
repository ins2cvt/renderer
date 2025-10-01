static float2 squarePositions[6] = float2[](
    float2(0.5, -0.5),
    float2(-0.5, 0.5),
    float2(-0.5, -0.5),
    float2(0.5, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5),
);

static float3 squareColors[6] = float3[](
    float3(0.0f, 0.0f, 1.0f),
    float3(0.0f, 1.0f, 0.0f),
    float3(1.0f, 0.0f, 0.0f),
    float3(0.0f, 0.0f, 1.0f),
    float3(1.0f, 1.0f, 1.0f),
    float3(0.0f, 1.0f, 0.0f),
);

struct VertexOutput {
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOutput vertexShader(uint vertexId: SV_VertexID) {
    VertexOutput output;
    output.position = float4(squarePositions[vertexId], 0.0, 1.0);
    output.color = squareColors[vertexId];
    return output;
};

[shader("fragment")]
float4 fragmentShader(VertexOutput vertexOutput) : SV_Target
{
    float4 color = float4(vertexOutput.color, 1.0f);
    return color;
}
