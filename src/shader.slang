struct UniformBuffer
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct VertexInput
{
    float3 pos;
    float3 color;
};

struct VertexOutput {
    float4 pos : SV_Position;
    float3 color;
};

[shader("vertex")]
VertexOutput vertexShader(VertexInput input) {
    VertexOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.pos, 1.0f))));
    output.color = input.color;
    return output;
};

[shader("fragment")]
float4 fragmentShader(VertexOutput vertexOutput) : SV_Target
{
    float4 color = float4(vertexOutput.color, 1.0f);
    return color;
}
