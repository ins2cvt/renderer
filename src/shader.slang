struct VertexInput
{
    float2 position;
    float3 color;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOutput vertexShader(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
};

[shader("fragment")]
float4 fragmentShader(VertexOutput vertexOutput) : SV_Target
{
    float4 color = float4(vertexOutput.color, 1.0f);
    return color;
}
